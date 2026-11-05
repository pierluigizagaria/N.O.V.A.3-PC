#include "launcher/language.h"
#include <windows.h>
#include <array>
#include <cwctype>
namespace nova::launcher {
namespace {
// Product translations are explicitly requested; engineering text remains English.
constexpr const wchar_t *strings[][6]={
 {L"Preparing your game",L"Preparazione del gioco",L"Préparation du jeu",L"Spiel wird vorbereitet",L"Preparando el juego",L"Preparando o jogo"},
 {L"Checking the files already on this PC.",L"Controllo i file",L"Vérification des fichiers sur ce PC.",L"Dateien auf diesem PC werden geprüft.",L"Comprobando los archivos de este PC.",L"Verificando os arquivos deste PC."},
 {L"Only the first time. Your game opens as soon as everything is ready.",L"Solo la prima volta. Appena è tutto pronto, il gioco si apre da solo.",L"Une seule fois. Le jeu s'ouvre dès que tout est prêt.",L"Nur beim ersten Mal. Das Spiel startet, sobald alles bereit ist.",L"Solo la primera vez. El juego se abre en cuanto todo está listo.",L"Só na primeira vez. O jogo abre assim que tudo estiver pronto."},
 {L"Select APK",L"Seleziona APK",L"Choisir l'APK",L"APK auswählen",L"Seleccionar APK",L"Selecionar APK"},
 {L"Select game data",L"Seleziona i dati",L"Choisir les données",L"Spieldaten wählen",L"Seleccionar datos",L"Selecionar dados"},
 {L"Cancel",L"Annulla",L"Annuler",L"Abbrechen",L"Cancelar",L"Cancelar"},
 {L"Try again",L"Riprova",L"Réessayer",L"Erneut versuchen",L"Reintentar",L"Tentar novamente"},
 {L"Preparing...",L"Preparazione...",L"Préparation...",L"Vorbereitung...",L"Preparando...",L"Preparando..."},
 {L"Waiting for your APK",L"In attesa del tuo APK",L"En attente de votre APK",L"Warten auf deine APK",L"Esperando tu APK",L"Aguardando seu APK"},
 {L"Waiting for game data",L"In attesa dei dati del gioco",L"En attente des données",L"Warten auf Spieldaten",L"Esperando los datos",L"Aguardando os dados"},
 {L"Choose the game package",L"Scegli il pacchetto di gioco",L"Choisissez le paquet du jeu",L"Spielpaket wählen",L"Elige el paquete del juego",L"Escolha o pacote do jogo"},
 {L"Add the game data",L"Aggiungi i dati del gioco",L"Ajoutez les données du jeu",L"Spieldaten hinzufügen",L"Añade los datos del juego",L"Adicione os dados do jogo"},
 {L"Select your N.O.V.A. 3 1.0.7 x86 APK. We take care of the rest.",L"Seleziona l'APK originale di N.O.V.A. 3 1.0.7 x86. Al resto pensa il launcher.",L"Choisissez votre APK N.O.V.A. 3 1.0.7 x86. Nous préparons la suite.",L"Wähle deine N.O.V.A. 3 1.0.7 x86-APK. Wir erledigen den Rest.",L"Selecciona tu APK de N.O.V.A. 3 1.0.7 x86. Nosotros hacemos el resto.",L"Selecione seu APK de N.O.V.A. 3 1.0.7 x86. Nós cuidamos do resto."},
 {L"The APK also needs its main and patch data. Select the data ZIP or either OBB.",L"Servono anche i dati main e patch. Seleziona lo ZIP dei dati o uno dei due OBB.",L"Il faut aussi les données main et patch. Choisissez le ZIP ou un des OBB.",L"Main- und Patch-Daten fehlen. Wähle die Daten-ZIP oder eine OBB-Datei.",L"También hacen falta los datos main y patch. Selecciona el ZIP o un OBB.",L"Também são necessários os dados main e patch. Selecione o ZIP ou um OBB."},
 {L"Setup needs attention",L"Controlliamo i file",L"Vérifions les fichiers",L"Dateien überprüfen",L"Revisemos los archivos",L"Vamos verificar os arquivos"},
 {L"Setup could not finish. Check the log, select another file or try again.",L"La preparazione non è terminata. Controlla il log, cambia file o riprova.",L"La préparation a échoué. Consultez le journal, changez de fichier ou réessayez.",L"Die Vorbereitung ist fehlgeschlagen. Prüfe das Protokoll oder wähle eine andere Datei.",L"La preparación no ha terminado. Revisa el registro, cambia de archivo o reintenta.",L"A preparação não terminou. Confira o registro, escolha outro arquivo ou tente novamente."},
 {L"Stopping safely",L"Chiusura in corso",L"Arrêt en cours",L"Wird sicher beendet",L"Cerrando con seguridad",L"Encerrando com segurança"},
 {L"Waiting for the current file operation. Your original files are unchanged.",L"Attendo la fine dell'operazione in corso. I file originali restano intatti.",L"Fin de l'opération en cours. Vos fichiers originaux restent intacts.",L"Der aktuelle Dateivorgang wird beendet. Deine Originaldateien bleiben unverändert.",L"Esperando a que termine la operación. Tus archivos originales siguen intactos.",L"Aguardando a operação atual. Seus arquivos originais permanecem intactos."},
 {L"Activity",L"Attività",L"Activité",L"Aktivität",L"Actividad",L"Atividade"},
 {L"Everything stays on your PC. Your saves are preserved.",L"Tutto resta sul tuo PC. I tuoi salvataggi sono al sicuro.",L"Tout reste sur votre PC. Vos sauvegardes sont conservées.",L"Alles bleibt auf deinem PC. Deine Spielstände bleiben erhalten.",L"Todo se queda en tu PC. Tus partidas se conservan.",L"Tudo fica no seu PC. Seus jogos salvos são preservados."},
 {L"First-time setup",L"Configurazione iniziale",L"Configuration initiale",L"Erste Einrichtung",L"Configuración inicial",L"Configuração inicial"},
 {L"N.O.V.A. 3 | Setup",L"N.O.V.A. 3 | Configurazione",L"N.O.V.A. 3 | Configuration",L"N.O.V.A. 3 | Einrichtung",L"N.O.V.A. 3 | Configuración",L"N.O.V.A. 3 | Configuração"},
 {L"Ready. Starting N.O.V.A. 3...",L"Pronto. Avvio di N.O.V.A. 3...",L"Prêt. Lancement de N.O.V.A. 3...",L"Bereit. N.O.V.A. 3 startet...",L"Listo. Iniciando N.O.V.A. 3...",L"Pronto. Iniciando N.O.V.A. 3..."},
 {L"N.O.V.A. 3 is already starting or running.",L"N.O.V.A. 3 è già in avvio o in esecuzione.",L"N.O.V.A. 3 est déjà ouvert ou en cours de lancement.",L"N.O.V.A. 3 wird bereits gestartet oder läuft.",L"N.O.V.A. 3 ya se está iniciando o ejecutando.",L"N.O.V.A. 3 já está iniciando ou em execução."},
 {L"The game stopped. See local\\logs\\launcher for details. Exit code: ",L"Il gioco si è chiuso. I dettagli sono in local\\logs\\launcher. Codice: ",L"Le jeu s'est arrêté. Détails dans local\\logs\\launcher. Code : ",L"Das Spiel wurde beendet. Details unter local\\logs\\launcher. Code: ",L"El juego se ha cerrado. Detalles en local\\logs\\launcher. Código: ",L"O jogo foi encerrado. Detalhes em local\\logs\\launcher. Código: "},
 {L"Current operation",L"Operazione in corso",L"Opération en cours",L"Aktueller Vorgang",L"Operación actual",L"Operação atual"},
 {L"Ready for your original game files.",L"Pronto per i tuoi file originali del gioco.",L"Prêt pour vos fichiers de jeu originaux.",L"Bereit für deine Original-Spieldateien.",L"Listo para tus archivos originales.",L"Pronto para seus arquivos originais."},
 {L"Verifying ",L"Verifica: ",L"Vérification : ",L"Prüfen: ",L"Verificando: ",L"Verificando: "},
 {L"Importing ",L"Importazione: ",L"Importation : ",L"Importieren: ",L"Importando: ",L"Importando: "},
 {L"Preparing ",L"Preparazione: ",L"Préparation : ",L"Vorbereiten: ",L"Preparando: ",L"Preparando: "},
 {L"APK verified",L"APK verificato",L"APK vérifié",L"APK geprüft",L"APK verificado",L"APK verificado"},
 {L"Checking APK",L"Controllo APK",L"Vérification APK",L"APK wird geprüft",L"Verificando APK",L"Verificando APK"},
 {L"Preparing files",L"Preparo i file",L"Préparation des fichiers",L"Dateien vorbereiten",L"Preparando archivos",L"Preparando arquivos"},
 {L"The game will start automatically.",L"Il gioco si avvierà automaticamente.",L"Le jeu démarrera automatiquement.",L"Das Spiel startet automatisch.",L"El juego se iniciará automáticamente.",L"O jogo iniciará automaticamente."},
 {L"Close",L"Chiudi",L"Fermer",L"Schließen",L"Cerrar",L"Fechar"},
 {L"Minimize",L"Riduci a icona",L"Réduire",L"Minimieren",L"Minimizar",L"Minimizar"},
 {L"APK ready",L"APK pronto",L"APK prêt",L"APK bereit",L"APK listo",L"APK pronto"},
 {L"Game data ready",L"Dati pronti",L"Données prêtes",L"Spieldaten bereit",L"Datos listos",L"Dados prontos"},
 {L"Preparing files...",L"Preparo i file...",L"Préparation des fichiers...",L"Dateien vorbereiten...",L"Preparando archivos...",L"Preparando arquivos..."},
 {L"Step %u of 3",L"Passo %u di 3",L"Étape %u sur 3",L"Schritt %u von 3",L"Paso %u de 3",L"Passo %u de 3"},
 {L"APK",L"APK",L"APK",L"APK",L"APK",L"APK"},
 {L"Data",L"Dati",L"Données",L"Daten",L"Datos",L"Dados"},
 {L"Setup",L"Preparazione",L"Préparation",L"Vorbereitung",L"Preparación",L"Preparação"},
 {L"Found next to the APK when possible",L"Cercati accanto all'APK",L"Recherchées à côté de l'APK",L"Werden neben der APK gesucht",L"Se buscan junto al APK",L"Procurados ao lado do APK"},
 {L"Then the game starts by itself",L"Poi il gioco parte da solo",L"Puis le jeu démarre seul",L"Danach startet das Spiel von selbst",L"Después el juego se inicia solo",L"Depois o jogo inicia sozinho"},
 {L"Drag the .apk file here",L"Trascina qui il file .apk",L"Glissez le fichier .apk ici",L"Ziehe die .apk-Datei hierher",L"Arrastra aquí el archivo .apk",L"Arraste o arquivo .apk para cá"},
 {L"Drag the data .zip or .obb here",L"Trascina qui lo .zip o l'.obb dei dati",L"Glissez ici le .zip ou l'.obb des données",L"Ziehe die Daten-.zip oder .obb hierher",L"Arrastra aquí el .zip o el .obb de datos",L"Arraste o .zip ou o .obb dos dados para cá"},
 {L"or",L"oppure",L"ou",L"oder",L"o",L"ou"},


 {L"Choose another file",L"Scegli un altro file",L"Choisir un autre fichier",L"Andere Datei wählen",L"Elegir otro archivo",L"Escolher outro arquivo"},

};
static_assert(std::size(strings)==static_cast<size_t>(Text::count));
std::wstring wide(std::string_view text) {
    const auto n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),n); return result;
}
}
const wchar_t *language_code(Language language) {
    constexpr const wchar_t *codes[]{L"EN",L"IT",L"FR",L"DE",L"ES",L"PT"};
    return codes[static_cast<size_t>(language)];
}
Language language_for_locale(std::wstring_view locale) {
    if(locale.size()<2) return Language::english;
    const std::wstring prefix{static_cast<wchar_t>(towlower(locale[0])),static_cast<wchar_t>(towlower(locale[1]))};
    if(prefix==L"it") return Language::italian;
    if(prefix==L"fr") return Language::french;
    if(prefix==L"de") return Language::german;
    if(prefix==L"es") return Language::spanish;
    if(prefix==L"pt") return Language::portuguese;
    return Language::english;
}
Language system_language() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if(!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(),SORT_DEFAULT),name,LOCALE_NAME_MAX_LENGTH,0)) return Language::english;
    return language_for_locale(name);
}
const wchar_t *tr(Language language,Text id) { return strings[static_cast<size_t>(id)][static_cast<size_t>(language)]; }
std::wstring localized_log(Language language,std::string_view message) {
    if(message=="Ready. Starting N.O.V.A. 3...") return tr(language,Text::ready_start);
    if(message=="Ready for your original game files.") return tr(language,Text::ready_files);
    if(message=="Checking APK compatibility") return std::wstring(tr(language,Text::verify))+L"APK 1.0.7 x86";
    if(message=="Importing the game library") return std::wstring(tr(language,Text::importing))+L"libNOVA3_neon.so";
    for(auto id:{Text::verify,Text::importing,Text::preparing_file}) {
        const auto prefix=wide(message.substr(0,std::min<size_t>(message.size(),10)));
        const std::wstring english=tr(Language::english,id);
        if(prefix.starts_with(english)) return std::wstring(tr(language,id))+wide(message.substr(english.size()));
    }
    return wide(message); // Exact low-level diagnostic retained, never guessed away.
}
std::wstring display_log(Language language,std::string_view message) {
    if(message=="Checking APK compatibility")return tr(language,Text::verify_apk);
    if(message.starts_with("Preparing ") || message.starts_with("Importing "))return tr(language,Text::extracting);
    if(message.starts_with("Verifying "))return tr(language,Text::checking);
    return localized_log(language,message);
}
}
