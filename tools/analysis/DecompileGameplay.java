// Decompile a bounded set of gameplay routines into private local research.
// Run after importing the pinned development-only ELF with analyzeHeadless.
// @category NOVA.Research
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class DecompileGameplay extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException("Expected private research output directory");
        }
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        if (!"a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e".equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Unsupported original ELF identity");
        }
        for (long offset : new long[]{0xc131dL, 0xc13f0L, 0x6ebd55L}) {
            Address thunkEntry = currentProgram.getImageBase().add(offset);
            disassemble(thunkEntry);
            Function thunk = getFunctionAt(thunkEntry);
            if (thunk == null) {
                thunk = createFunction(thunkEntry, null);
            }
            if (thunk != null) {
                thunk.setInline(true);
            }
        }
        long[][] routines = {
            {0x382890L, 5555}, // Spawn selection.
            {0x5c5090L, 9268}, // Target events, including multiplayer explosion.
            {0x6efd90L, 309},  // Closest animated body part.
            {0x25b570L, 2859}, // Projectile death / explosion emission.
        };
        String[] names = {"spawn-selection", "target-events", "closest-body-part", "projectile-death"};
        // Optional bounded original-address:size:name selections reuse the
        // existing project without redoing unrelated gameplay research.
        if (args.length > 1) {
            routines = new long[args.length - 1][2];
            names = new String[args.length - 1];
            for (int i = 1; i < args.length; i++) {
                String[] spec = args[i].split(":", 3);
                if (spec.length != 3 || !spec[2].matches("[a-z0-9-]+")) {
                    throw new IllegalArgumentException("Expected address:size:name");
                }
                routines[i - 1][0] = Long.decode(spec[0]);
                routines[i - 1][1] = Long.decode(spec[1]);
                names[i - 1] = spec[2];
            }
        }
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(decompiler.getLastMessage());
        }
        try {
            for (int i = 0; i < routines.length; i++) {
                monitor.checkCancelled();
                if (routines[i][1] < 1) {
                    throw new IllegalArgumentException("Expected a positive verified routine extent");
                }
                // This ET_DYN image has original base zero. Ghidra relocates
                // shared objects to its selected image base on import.
                Address entry = currentProgram.getImageBase().add(routines[i][0]);
                AddressSet body = new AddressSet(entry, entry.add(routines[i][1] - 1));
                // ELF local labels can be imported as one-byte functions inside
                // the symbol's verified extent. Keep labels, remove only those
                // interior function definitions before reconstructing the body.
                java.util.Iterator<Function> overlaps = currentProgram.getFunctionManager().getFunctionsOverlapping(body);
                java.util.ArrayList<Address> interior = new java.util.ArrayList<>();
                while (overlaps.hasNext()) {
                    Address address = overlaps.next().getEntryPoint();
                    if (!address.equals(entry) && body.contains(address)) {
                        interior.add(address);
                    }
                }
                for (Address address : interior) {
                    currentProgram.getFunctionManager().removeFunction(address);
                }
                disassemble(entry);
                Function function = getFunctionAt(entry);
                if (function == null) {
                    function = createFunction(entry, null);
                }
                if (function == null) {
                    throw new IllegalStateException("Missing function at " + entry);
                }
                function.setBody(body);
                DecompileResults result = decompiler.decompileFunction(function, routines[i][1] > 20000 ? 180 : 45, monitor);
                if (!result.decompileCompleted()) {
                    throw new IllegalStateException(names[i] + ": " + result.getErrorMessage());
                }
                Files.writeString(output.resolve(names[i] + ".c"), result.getDecompiledFunction().getC());
                println("Decompiled " + names[i] + " at " + entry);
            }
        } finally {
            decompiler.dispose();
        }
    }
}
