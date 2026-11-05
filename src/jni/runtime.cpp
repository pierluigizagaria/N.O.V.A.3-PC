#include "jni/runtime.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace nova::jni {
namespace {
thread_local std::unique_ptr<Environment> current;
void __cdecl cleanup_thread() {
    if (current)
        current->runtime->detach();
}
const char *const slot_names[] = {
#include "jni/slots.inc"
};
std::string encode(const std::u16string &text) {
    std::string out;
    for (uint16_t c : text) {
        if (c && c < 128)
            out += static_cast<char>(c);
        else if (c < 2048) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 63));
        } else {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 63));
            out += static_cast<char>(0x80 | (c & 63));
        }
    }
    return out;
}
std::u16string decode(const char *utf) {
    std::u16string out;
    if (!utf)
        return out;
    auto *p = reinterpret_cast<const unsigned char *>(utf);
    while (*p) {
        uint16_t c = *p++;
        if (c >= 0xc0 && c < 0xe0 && p[0] && (p[0] & 0xc0) == 0x80) {
            c = static_cast<uint16_t>(((c & 31) << 6) | (p[0] & 63));
            ++p;
        } else if (c >= 0xe0 && c < 0xf0 && p[0] && p[1] && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) {
            c = static_cast<uint16_t>(((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63));
            p += 2;
        } else if (c >= 128) {
            c = 0xfffd;
        }
        out += static_cast<char16_t>(c);
    }
    return out;
}
std::vector<char> argument_types(const std::string &signature) {
    std::vector<char> types;
    size_t i = 1;
    while (i < signature.size() && signature[i] != ')') {
        char c = signature[i++];
        if (c == '[') {
            while (i < signature.size() && signature[i] == '[')
                ++i;
            if (i < signature.size() && signature[i] == 'L') {
                auto end = signature.find(';', i);
                if (end == std::string::npos)
                    break;
                i = end + 1;
            } else
                ++i;
            types.push_back('L');
        } else if (c == 'L') {
            auto end = signature.find(';', i);
            if (end == std::string::npos)
                break;
            i = end + 1;
            types.push_back('L');
        } else
            types.push_back(c);
    }
    return types;
}
std::vector<Value> arguments(Method *m, char *raw) {
    std::vector<Value> result;
    if (!m)
        return result;
    for (char c : argument_types(m->signature)) {
        Value v;
        switch (c) {
        case 'J':
            std::memcpy(&v.j, raw, 8);
            raw += 8;
            break;
        case 'D':
            std::memcpy(&v.d, raw, 8);
            raw += 8;
            break;
        case 'F': {
            double d;
            std::memcpy(&d, raw, 8);
            v.f = static_cast<float>(d);
            raw += 8;
            break;
        }
        default:
            std::memcpy(&v.i, raw, 4);
            raw += 4;
            break;
        }
        result.push_back(v);
    }
    return result;
}
std::vector<Value> arguments_a(Method *m, const Value *a) {
    size_t n = m ? argument_types(m->signature).size() : 0;
    return n ? std::vector<Value>(a, a + n) : std::vector<Value>{};
}
int __cdecl version(Environment *) { return 0x10006; }
Handle __cdecl find_class(Environment *e, const char *name) {
    return e->runtime->find_class(name ? name : "");
}
Handle __cdecl global_ref(Environment *e, Handle h) {
    return e->runtime->reference(e->runtime->object(h), 2);
}
Handle __cdecl local_ref(Environment *e, Handle h) { return e->runtime->reference(e->runtime->object(h)); }
Handle __cdecl weak_ref(Environment *e, Handle h) { return e->runtime->reference(e->runtime->object(h), 3); }
void __cdecl delete_global(Environment *e, Handle h) { e->runtime->release(h, 2); }
void __cdecl delete_local(Environment *e, Handle h) { e->runtime->release(h, 1); }
void __cdecl delete_weak(Environment *e, Handle h) { e->runtime->release(h, 3); }
uint8_t __cdecl same(Environment *e, Handle a, Handle b) {
    return e->runtime->object(a) == e->runtime->object(b);
}
int __cdecl ref_type(Environment *e, Handle h) { return e->runtime->reference_type(h); }
int __cdecl ensure(Environment *e, int capacity) {
    if (capacity < 0)
        return -1;
    e->frames.back().reserve(e->frames.back().size() + capacity);
    return 0;
}
int __cdecl push_frame(Environment *e, int capacity) {
    if (capacity < 0)
        return -1;
    e->frames.emplace_back();
    return ensure(e, capacity);
}
Handle __cdecl pop_frame(Environment *e, Handle keep) {
    auto value = e->runtime->object(keep);
    if (e->frames.size() <= 1) {
        e->runtime->exception("java/lang/IllegalStateException", "Unbalanced PopLocalFrame");
        return nullptr;
    }
    auto handles = std::move(e->frames.back());
    e->frames.pop_back();
    for (auto h : handles)
        e->runtime->release(h, 1);
    return e->runtime->reference(value);
}
Handle __cdecl allocate(Environment *e, Handle c) {
    auto klass = e->runtime->object(c);
    if (!klass)
        return nullptr;
    auto o = std::make_shared<Object>();
    o->klass = klass;
    return e->runtime->reference(o);
}
Handle __cdecl object_class(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o ? e->runtime->reference(o->kind == Object::Kind::klass ? e->runtime->klass("java/lang/Class")
                                                                    : o->klass)
             : nullptr;
}
Handle __cdecl superclass(Environment *e, Handle c) {
    auto o = e->runtime->object(c);
    return !o || o->name == "java/lang/Object" ? nullptr : e->runtime->find_class("java/lang/Object");
}
uint8_t __cdecl instance(Environment *e, Handle o, Handle c) {
    auto obj = e->runtime->object(o), cls = e->runtime->object(c);
    return !obj || obj->klass == cls || (cls && cls->name == "java/lang/Object");
}
uint8_t __cdecl assignable(Environment *e, Handle a, Handle b) {
    auto one = e->runtime->object(a), two = e->runtime->object(b);
    return one == two || (two && two->name == "java/lang/Object");
}
Method *__cdecl method_id(Environment *e, Handle c, const char *n, const char *s) {
    return e->runtime->method(c, n, s, false);
}
Method *__cdecl static_method_id(Environment *e, Handle c, const char *n, const char *s) {
    return e->runtime->method(c, n, s, true);
}
Field *__cdecl field_id(Environment *e, Handle c, const char *n, const char *s) {
    return e->runtime->field(c, n, s, false);
}
Field *__cdecl static_field_id(Environment *e, Handle c, const char *n, const char *s) {
    return e->runtime->field(c, n, s, true);
}
int __cdecl throw_object(Environment *e, Handle h) {
    e->exception = e->runtime->object(h);
    return e->exception ? 0 : -1;
}
int __cdecl throw_new(Environment *e, Handle c, const char *message) {
    auto o = std::make_shared<Object>();
    o->klass = e->runtime->object(c);
    o->name = message ? message : "";
    e->exception = o;
    return 0;
}
Handle __cdecl exception_occurred(Environment *e) { return e->runtime->reference(e->exception); }
void __cdecl exception_clear(Environment *e) { e->exception.reset(); }
uint8_t __cdecl exception_check(Environment *e) { return e->exception != nullptr; }
void __cdecl exception_describe(Environment *e) {
    if (e->exception)
        std::fprintf(stderr, "JNI exception: %s: %s\n",
                     e->exception->klass ? e->exception->klass->name.c_str() : "unknown",
                     e->exception->name.c_str());
}
[[noreturn]] void __cdecl fatal(Environment *, const char *text) {
    std::fprintf(stderr, "JNI FatalError: %s\n", text ? text : "");
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 72);
    __assume(false);
}
Handle __cdecl new_string(Environment *e, const char16_t *p, int n) {
    if (n < 0) {
        e->runtime->exception("java/lang/IllegalArgumentException", "negative string length");
        return nullptr;
    }
    return e->runtime->string16(p, n);
}
Handle __cdecl new_utf(Environment *e, const char *p) { return p ? e->runtime->string(p) : nullptr; }
int __cdecl string_length(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o ? static_cast<int>(o->text.size()) : 0;
}
int __cdecl utf_length(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o ? static_cast<int>(o->utf.size()) : 0;
}
const char16_t *__cdecl string_chars(Environment *e, Handle h, uint8_t *copy) {
    auto o = e->runtime->object(h);
    if (copy)
        *copy = 0;
    return o ? o->text.c_str() : nullptr;
}
const char *__cdecl utf_chars(Environment *e, Handle h, uint8_t *copy) {
    auto o = e->runtime->object(h);
    if (copy)
        *copy = 0;
    return o ? o->utf.c_str() : nullptr;
}
void __cdecl release_chars(Environment *, Handle, const void *) {}
bool bounds(Environment *e, int start, int length, size_t size) {
    if (start < 0 || length < 0 || static_cast<size_t>(start) > size ||
        static_cast<size_t>(length) > size - start) {
        e->runtime->exception("java/lang/ArrayIndexOutOfBoundsException", "JNI region");
        return false;
    }
    return true;
}
void __cdecl string_region(Environment *e, Handle h, int start, int n, char16_t *out) {
    auto o = e->runtime->object(h);
    if (o && bounds(e, start, n, o->text.size()))
        std::memcpy(out, o->text.data() + start, n * 2);
}
void __cdecl utf_region(Environment *e, Handle h, int start, int n, char *out) {
    auto o = e->runtime->object(h);
    if (o && bounds(e, start, n, o->text.size())) {
        auto s = encode(o->text.substr(start, n));
        std::memcpy(out, s.data(), s.size());
    }
}
int __cdecl array_length(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o ? o->length : 0;
}
Handle array_new(Environment *e, int n, size_t size, char type) {
    if (n < 0 || static_cast<uint64_t>(n) * size > INT32_MAX) {
        e->runtime->exception("java/lang/NegativeArraySizeException", "array size");
        return nullptr;
    }
    auto o = std::make_shared<Object>();
    o->kind = Object::Kind::array;
    o->klass = e->runtime->klass(std::string("[") + type);
    o->length = n;
    o->element_size = size;
    o->element_type = type;
    o->bytes.resize(static_cast<size_t>(n) * size);
    return e->runtime->reference(o);
}
Handle __cdecl object_array_new(Environment *e, int n, Handle c, Handle initial) {
    if (n < 0) {
        e->runtime->exception("java/lang/NegativeArraySizeException", "array size");
        return nullptr;
    }
    auto o = std::make_shared<Object>();
    o->kind = Object::Kind::object_array;
    auto k = e->runtime->object(c);
    o->klass = e->runtime->klass("[L" + (k ? k->name : "java/lang/Object") + ";");
    o->length = n;
    o->objects.resize(n, e->runtime->object(initial));
    return e->runtime->reference(o);
}
Handle __cdecl array_object_get(Environment *e, Handle h, int index) {
    auto o = e->runtime->object(h);
    if (!o || !bounds(e, index, 1, o->objects.size()))
        return nullptr;
    return e->runtime->reference(o->objects[index]);
}
void __cdecl array_object_set(Environment *e, Handle h, int index, Handle v) {
    auto o = e->runtime->object(h);
    if (o && bounds(e, index, 1, o->objects.size()))
        o->objects[index] = e->runtime->object(v);
}
void *__cdecl array_elements(Environment *e, Handle h, uint8_t *copy) {
    auto o = e->runtime->object(h);
    if (copy)
        *copy = 0;
    return o ? o->bytes.data() : nullptr;
}
void __cdecl array_release(Environment *, Handle, void *, int) {}
void __cdecl array_get_region(Environment *e, Handle h, int start, int n, void *out) {
    auto o = e->runtime->object(h);
    if (o && bounds(e, start, n, o->length))
        std::memcpy(out, o->bytes.data() + start * o->element_size, n * o->element_size);
}
void __cdecl array_set_region(Environment *e, Handle h, int start, int n, const void *in) {
    auto o = e->runtime->object(h);
    if (o && bounds(e, start, n, o->length))
        std::memcpy(o->bytes.data() + start * o->element_size, in, n * o->element_size);
}
Handle __cdecl direct_new(Environment *e, void *address, int64_t capacity) {
    if (capacity < 0)
        return nullptr;
    auto o = std::make_shared<Object>();
    o->kind = Object::Kind::direct;
    o->klass = e->runtime->klass("java/nio/ByteBuffer");
    o->direct = address;
    o->capacity = capacity;
    return e->runtime->reference(o);
}
void *__cdecl direct_address(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o ? o->direct : nullptr;
}
int64_t __cdecl direct_capacity(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    return o && o->kind == Object::Kind::direct ? o->capacity : -1;
}
int __cdecl get_vm(Environment *e, VM **vm) {
    *vm = e->runtime->vm();
    return 0;
}
int __cdecl monitor_enter(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    if (!o)
        return -1;
    o->monitor.lock();
    return 0;
}
int __cdecl monitor_exit(Environment *e, Handle h) {
    auto o = e->runtime->object(h);
    if (!o)
        return -1;
    o->monitor.unlock();
    return 0;
}
struct NativeMethod {
    const char *name;
    const char *signature;
    uint32_t function;
};
int __cdecl register_natives(Environment *e, Handle c, const NativeMethod *list, int n) {
    if (n < 0)
        return -1;
    for (int i = 0; i < n; ++i) {
        auto *m = e->runtime->method(c, list[i].name, list[i].signature, true);
        m->native = list[i].function;
        e->runtime->method(c, list[i].name, list[i].signature, false)->native = list[i].function;
    }
    return 0;
}
int __cdecl attach(VM *vm, Environment **out, void *) {
    if (!out)
        return -1;
    *out = vm->runtime->attach();
    return 0;
}
int __cdecl detach(VM *vm) { return vm->runtime->detach(); }
int __cdecl get_env(VM *vm, Environment **out, int requested) {
    if (!out)
        return -1;
    *out = nullptr;
    if (requested != 0x10001 && requested != 0x10002 && requested != 0x10004 && requested != 0x10006)
        return -3;
    *out = vm->runtime->env();
    return *out ? 0 : -2;
}
Handle __cdecl call_Object_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).l;
}
Handle __cdecl call_Object_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).l;
}
Handle __cdecl call_Object(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).l;
}
Handle __cdecl nonvirtual_Object_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Object_a(e, o, m, a);
}
Handle __cdecl nonvirtual_Object_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Object_v(e, o, m, a);
}
Handle __cdecl nonvirtual_Object(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).l;
}
Handle __cdecl field_get_Object(Environment *e, Handle h, Field *f) {
    auto value = f->is_static ? f->object : e->runtime->object(h)->object_fields[f];
    return e->runtime->reference(value);
}
void __cdecl field_set_Object(Environment *e, Handle h, Field *f, Handle value) {
    auto o = e->runtime->object(value);
    if (f->is_static)
        f->object = o;
    else
        e->runtime->object(h)->object_fields[f] = o;
}
uint8_t __cdecl call_Boolean_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).z;
}
uint8_t __cdecl call_Boolean_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).z;
}
uint8_t __cdecl call_Boolean(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).z;
}
uint8_t __cdecl nonvirtual_Boolean_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Boolean_a(e, o, m, a);
}
uint8_t __cdecl nonvirtual_Boolean_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Boolean_v(e, o, m, a);
}
uint8_t __cdecl nonvirtual_Boolean(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).z;
}
uint8_t __cdecl field_get_Boolean(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).z;
}
void __cdecl field_set_Boolean(Environment *e, Handle h, Field *f, uint8_t value) {
    if (f->is_static)
        f->value.z = value;
    else
        e->runtime->object(h)->fields[f].z = value;
}
Handle __cdecl array_Boolean(Environment *e, int n) { return array_new(e, n, sizeof(uint8_t), 'Z'); }
int8_t __cdecl call_Byte_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).b;
}
int8_t __cdecl call_Byte_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).b;
}
int8_t __cdecl call_Byte(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).b;
}
int8_t __cdecl nonvirtual_Byte_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Byte_a(e, o, m, a);
}
int8_t __cdecl nonvirtual_Byte_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Byte_v(e, o, m, a);
}
int8_t __cdecl nonvirtual_Byte(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).b;
}
int8_t __cdecl field_get_Byte(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).b;
}
void __cdecl field_set_Byte(Environment *e, Handle h, Field *f, int8_t value) {
    if (f->is_static)
        f->value.b = value;
    else
        e->runtime->object(h)->fields[f].b = value;
}
Handle __cdecl array_Byte(Environment *e, int n) { return array_new(e, n, sizeof(int8_t), 'B'); }
uint16_t __cdecl call_Char_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).c;
}
uint16_t __cdecl call_Char_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).c;
}
uint16_t __cdecl call_Char(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).c;
}
uint16_t __cdecl nonvirtual_Char_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Char_a(e, o, m, a);
}
uint16_t __cdecl nonvirtual_Char_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Char_v(e, o, m, a);
}
uint16_t __cdecl nonvirtual_Char(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).c;
}
uint16_t __cdecl field_get_Char(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).c;
}
void __cdecl field_set_Char(Environment *e, Handle h, Field *f, uint16_t value) {
    if (f->is_static)
        f->value.c = value;
    else
        e->runtime->object(h)->fields[f].c = value;
}
Handle __cdecl array_Char(Environment *e, int n) { return array_new(e, n, sizeof(uint16_t), 'C'); }
int16_t __cdecl call_Short_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).s;
}
int16_t __cdecl call_Short_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).s;
}
int16_t __cdecl call_Short(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).s;
}
int16_t __cdecl nonvirtual_Short_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Short_a(e, o, m, a);
}
int16_t __cdecl nonvirtual_Short_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Short_v(e, o, m, a);
}
int16_t __cdecl nonvirtual_Short(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).s;
}
int16_t __cdecl field_get_Short(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).s;
}
void __cdecl field_set_Short(Environment *e, Handle h, Field *f, int16_t value) {
    if (f->is_static)
        f->value.s = value;
    else
        e->runtime->object(h)->fields[f].s = value;
}
Handle __cdecl array_Short(Environment *e, int n) { return array_new(e, n, sizeof(int16_t), 'S'); }
int32_t __cdecl call_Int_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).i;
}
int32_t __cdecl call_Int_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).i;
}
int32_t __cdecl call_Int(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).i;
}
int32_t __cdecl nonvirtual_Int_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Int_a(e, o, m, a);
}
int32_t __cdecl nonvirtual_Int_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Int_v(e, o, m, a);
}
int32_t __cdecl nonvirtual_Int(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).i;
}
int32_t __cdecl field_get_Int(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).i;
}
void __cdecl field_set_Int(Environment *e, Handle h, Field *f, int32_t value) {
    if (f->is_static)
        f->value.i = value;
    else
        e->runtime->object(h)->fields[f].i = value;
}
Handle __cdecl array_Int(Environment *e, int n) { return array_new(e, n, sizeof(int32_t), 'I'); }
int64_t __cdecl call_Long_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).j;
}
int64_t __cdecl call_Long_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).j;
}
int64_t __cdecl call_Long(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).j;
}
int64_t __cdecl nonvirtual_Long_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Long_a(e, o, m, a);
}
int64_t __cdecl nonvirtual_Long_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Long_v(e, o, m, a);
}
int64_t __cdecl nonvirtual_Long(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).j;
}
int64_t __cdecl field_get_Long(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).j;
}
void __cdecl field_set_Long(Environment *e, Handle h, Field *f, int64_t value) {
    if (f->is_static)
        f->value.j = value;
    else
        e->runtime->object(h)->fields[f].j = value;
}
Handle __cdecl array_Long(Environment *e, int n) { return array_new(e, n, sizeof(int64_t), 'J'); }
float __cdecl call_Float_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).f;
}
float __cdecl call_Float_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).f;
}
float __cdecl call_Float(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).f;
}
float __cdecl nonvirtual_Float_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Float_a(e, o, m, a);
}
float __cdecl nonvirtual_Float_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Float_v(e, o, m, a);
}
float __cdecl nonvirtual_Float(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).f;
}
float __cdecl field_get_Float(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).f;
}
void __cdecl field_set_Float(Environment *e, Handle h, Field *f, float value) {
    if (f->is_static)
        f->value.f = value;
    else
        e->runtime->object(h)->fields[f].f = value;
}
Handle __cdecl array_Float(Environment *e, int n) { return array_new(e, n, sizeof(float), 'F'); }
double __cdecl call_Double_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    return e->runtime->invoke(o, m, values).d;
}
double __cdecl call_Double_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    return e->runtime->invoke(o, m, values).d;
}
double __cdecl call_Double(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).d;
}
double __cdecl nonvirtual_Double_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Double_a(e, o, m, a);
}
double __cdecl nonvirtual_Double_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Double_v(e, o, m, a);
}
double __cdecl nonvirtual_Double(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    return e->runtime->invoke(o, m, values).d;
}
double __cdecl field_get_Double(Environment *e, Handle h, Field *f) {
    return (f->is_static ? f->value : e->runtime->object(h)->fields[f]).d;
}
void __cdecl field_set_Double(Environment *e, Handle h, Field *f, double value) {
    if (f->is_static)
        f->value.d = value;
    else
        e->runtime->object(h)->fields[f].d = value;
}
Handle __cdecl array_Double(Environment *e, int n) { return array_new(e, n, sizeof(double), 'D'); }
void __cdecl call_Void_a(Environment *e, Handle o, Method *m, const Value *a) {
    auto values = arguments_a(m, a);
    e->runtime->invoke(o, m, values);
}
void __cdecl call_Void_v(Environment *e, Handle o, Method *m, char *a) {
    auto values = arguments(m, a);
    e->runtime->invoke(o, m, values);
}
void __cdecl call_Void(Environment *e, Handle o, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    e->runtime->invoke(o, m, values);
}
void __cdecl nonvirtual_Void_a(Environment *e, Handle o, Handle, Method *m, const Value *a) {
    return call_Void_a(e, o, m, a);
}
void __cdecl nonvirtual_Void_v(Environment *e, Handle o, Handle, Method *m, char *a) {
    return call_Void_v(e, o, m, a);
}
void __cdecl nonvirtual_Void(Environment *e, Handle o, Handle, Method *m, ...) {
    va_list a;
    va_start(a, m);
    auto values = arguments(m, a);
    va_end(a);
    e->runtime->invoke(o, m, values);
}
Handle __cdecl new_object_a(Environment *e, Handle c, Method *m, const Value *a) {
    Handle o = allocate(e, c);
    call_Void_a(e, o, m, a);
    return o;
}
Handle __cdecl new_object_v(Environment *e, Handle c, Method *m, char *a) {
    Handle o = allocate(e, c);
    call_Void_v(e, o, m, a);
    return o;
}
Handle __cdecl new_object(Environment *e, Handle c, Method *m, ...) {
    va_list a;
    va_start(a, m);
    Handle o = new_object_v(e, c, m, a);
    va_end(a);
    return o;
}
} // namespace
Runtime::Runtime(const ElfImage &image) : image_(image), traps_(image) {
    bionic::set_thread_cleanup(cleanup_thread);
    for (size_t i = 0; i < 233; ++i)
        env_table_[i] = traps_.add(std::string("JNI.") + slot_names[i]);
    const char *vm_names[] = {
        "reserved0",           "reserved1",           "reserved2", "DestroyJavaVM",
        "AttachCurrentThread", "DetachCurrentThread", "GetEnv",    "AttachCurrentThreadAsDaemon"};
    for (size_t i = 0; i < 8; ++i)
        vm_table_[i] = traps_.add(std::string("JavaVM.") + vm_names[i]);
    auto bind = [&](const char *name, const void *function) {
        for (size_t i = 0; i < 233; ++i)
            if (std::strcmp(slot_names[i], name) == 0) {
                env_table_[i] = reinterpret_cast<uint32_t>(function);
                return;
            }
        throw std::runtime_error("Unknown JNI slot");
    };
#define B(name, fn) bind(name, reinterpret_cast<const void *>(&jni::fn))
    B("CallObjectMethod", call_Object);
    B("CallObjectMethodV", call_Object_v);
    B("CallObjectMethodA", call_Object_a);
    B("CallStaticObjectMethod", call_Object);
    B("CallStaticObjectMethodV", call_Object_v);
    B("CallStaticObjectMethodA", call_Object_a);
    B("CallNonvirtualObjectMethod", nonvirtual_Object);
    B("CallNonvirtualObjectMethodV", nonvirtual_Object_v);
    B("CallNonvirtualObjectMethodA", nonvirtual_Object_a);
    B("GetObjectField", field_get_Object);
    B("SetObjectField", field_set_Object);
    B("GetStaticObjectField", field_get_Object);
    B("SetStaticObjectField", field_set_Object);
    B("CallBooleanMethod", call_Boolean);
    B("CallBooleanMethodV", call_Boolean_v);
    B("CallBooleanMethodA", call_Boolean_a);
    B("CallStaticBooleanMethod", call_Boolean);
    B("CallStaticBooleanMethodV", call_Boolean_v);
    B("CallStaticBooleanMethodA", call_Boolean_a);
    B("CallNonvirtualBooleanMethod", nonvirtual_Boolean);
    B("CallNonvirtualBooleanMethodV", nonvirtual_Boolean_v);
    B("CallNonvirtualBooleanMethodA", nonvirtual_Boolean_a);
    B("GetBooleanField", field_get_Boolean);
    B("SetBooleanField", field_set_Boolean);
    B("GetStaticBooleanField", field_get_Boolean);
    B("SetStaticBooleanField", field_set_Boolean);
    B("NewBooleanArray", array_Boolean);
    B("GetBooleanArrayElements", array_elements);
    B("ReleaseBooleanArrayElements", array_release);
    B("GetBooleanArrayRegion", array_get_region);
    B("SetBooleanArrayRegion", array_set_region);
    B("CallByteMethod", call_Byte);
    B("CallByteMethodV", call_Byte_v);
    B("CallByteMethodA", call_Byte_a);
    B("CallStaticByteMethod", call_Byte);
    B("CallStaticByteMethodV", call_Byte_v);
    B("CallStaticByteMethodA", call_Byte_a);
    B("CallNonvirtualByteMethod", nonvirtual_Byte);
    B("CallNonvirtualByteMethodV", nonvirtual_Byte_v);
    B("CallNonvirtualByteMethodA", nonvirtual_Byte_a);
    B("GetByteField", field_get_Byte);
    B("SetByteField", field_set_Byte);
    B("GetStaticByteField", field_get_Byte);
    B("SetStaticByteField", field_set_Byte);
    B("NewByteArray", array_Byte);
    B("GetByteArrayElements", array_elements);
    B("ReleaseByteArrayElements", array_release);
    B("GetByteArrayRegion", array_get_region);
    B("SetByteArrayRegion", array_set_region);
    B("CallCharMethod", call_Char);
    B("CallCharMethodV", call_Char_v);
    B("CallCharMethodA", call_Char_a);
    B("CallStaticCharMethod", call_Char);
    B("CallStaticCharMethodV", call_Char_v);
    B("CallStaticCharMethodA", call_Char_a);
    B("CallNonvirtualCharMethod", nonvirtual_Char);
    B("CallNonvirtualCharMethodV", nonvirtual_Char_v);
    B("CallNonvirtualCharMethodA", nonvirtual_Char_a);
    B("GetCharField", field_get_Char);
    B("SetCharField", field_set_Char);
    B("GetStaticCharField", field_get_Char);
    B("SetStaticCharField", field_set_Char);
    B("NewCharArray", array_Char);
    B("GetCharArrayElements", array_elements);
    B("ReleaseCharArrayElements", array_release);
    B("GetCharArrayRegion", array_get_region);
    B("SetCharArrayRegion", array_set_region);
    B("CallShortMethod", call_Short);
    B("CallShortMethodV", call_Short_v);
    B("CallShortMethodA", call_Short_a);
    B("CallStaticShortMethod", call_Short);
    B("CallStaticShortMethodV", call_Short_v);
    B("CallStaticShortMethodA", call_Short_a);
    B("CallNonvirtualShortMethod", nonvirtual_Short);
    B("CallNonvirtualShortMethodV", nonvirtual_Short_v);
    B("CallNonvirtualShortMethodA", nonvirtual_Short_a);
    B("GetShortField", field_get_Short);
    B("SetShortField", field_set_Short);
    B("GetStaticShortField", field_get_Short);
    B("SetStaticShortField", field_set_Short);
    B("NewShortArray", array_Short);
    B("GetShortArrayElements", array_elements);
    B("ReleaseShortArrayElements", array_release);
    B("GetShortArrayRegion", array_get_region);
    B("SetShortArrayRegion", array_set_region);
    B("CallIntMethod", call_Int);
    B("CallIntMethodV", call_Int_v);
    B("CallIntMethodA", call_Int_a);
    B("CallStaticIntMethod", call_Int);
    B("CallStaticIntMethodV", call_Int_v);
    B("CallStaticIntMethodA", call_Int_a);
    B("CallNonvirtualIntMethod", nonvirtual_Int);
    B("CallNonvirtualIntMethodV", nonvirtual_Int_v);
    B("CallNonvirtualIntMethodA", nonvirtual_Int_a);
    B("GetIntField", field_get_Int);
    B("SetIntField", field_set_Int);
    B("GetStaticIntField", field_get_Int);
    B("SetStaticIntField", field_set_Int);
    B("NewIntArray", array_Int);
    B("GetIntArrayElements", array_elements);
    B("ReleaseIntArrayElements", array_release);
    B("GetIntArrayRegion", array_get_region);
    B("SetIntArrayRegion", array_set_region);
    B("CallLongMethod", call_Long);
    B("CallLongMethodV", call_Long_v);
    B("CallLongMethodA", call_Long_a);
    B("CallStaticLongMethod", call_Long);
    B("CallStaticLongMethodV", call_Long_v);
    B("CallStaticLongMethodA", call_Long_a);
    B("CallNonvirtualLongMethod", nonvirtual_Long);
    B("CallNonvirtualLongMethodV", nonvirtual_Long_v);
    B("CallNonvirtualLongMethodA", nonvirtual_Long_a);
    B("GetLongField", field_get_Long);
    B("SetLongField", field_set_Long);
    B("GetStaticLongField", field_get_Long);
    B("SetStaticLongField", field_set_Long);
    B("NewLongArray", array_Long);
    B("GetLongArrayElements", array_elements);
    B("ReleaseLongArrayElements", array_release);
    B("GetLongArrayRegion", array_get_region);
    B("SetLongArrayRegion", array_set_region);
    B("CallFloatMethod", call_Float);
    B("CallFloatMethodV", call_Float_v);
    B("CallFloatMethodA", call_Float_a);
    B("CallStaticFloatMethod", call_Float);
    B("CallStaticFloatMethodV", call_Float_v);
    B("CallStaticFloatMethodA", call_Float_a);
    B("CallNonvirtualFloatMethod", nonvirtual_Float);
    B("CallNonvirtualFloatMethodV", nonvirtual_Float_v);
    B("CallNonvirtualFloatMethodA", nonvirtual_Float_a);
    B("GetFloatField", field_get_Float);
    B("SetFloatField", field_set_Float);
    B("GetStaticFloatField", field_get_Float);
    B("SetStaticFloatField", field_set_Float);
    B("NewFloatArray", array_Float);
    B("GetFloatArrayElements", array_elements);
    B("ReleaseFloatArrayElements", array_release);
    B("GetFloatArrayRegion", array_get_region);
    B("SetFloatArrayRegion", array_set_region);
    B("CallDoubleMethod", call_Double);
    B("CallDoubleMethodV", call_Double_v);
    B("CallDoubleMethodA", call_Double_a);
    B("CallStaticDoubleMethod", call_Double);
    B("CallStaticDoubleMethodV", call_Double_v);
    B("CallStaticDoubleMethodA", call_Double_a);
    B("CallNonvirtualDoubleMethod", nonvirtual_Double);
    B("CallNonvirtualDoubleMethodV", nonvirtual_Double_v);
    B("CallNonvirtualDoubleMethodA", nonvirtual_Double_a);
    B("GetDoubleField", field_get_Double);
    B("SetDoubleField", field_set_Double);
    B("GetStaticDoubleField", field_get_Double);
    B("SetStaticDoubleField", field_set_Double);
    B("NewDoubleArray", array_Double);
    B("GetDoubleArrayElements", array_elements);
    B("ReleaseDoubleArrayElements", array_release);
    B("GetDoubleArrayRegion", array_get_region);
    B("SetDoubleArrayRegion", array_set_region);
    B("CallVoidMethod", call_Void);
    B("CallVoidMethodV", call_Void_v);
    B("CallVoidMethodA", call_Void_a);
    B("CallStaticVoidMethod", call_Void);
    B("CallStaticVoidMethodV", call_Void_v);
    B("CallStaticVoidMethodA", call_Void_a);
    B("CallNonvirtualVoidMethod", nonvirtual_Void);
    B("CallNonvirtualVoidMethodV", nonvirtual_Void_v);
    B("CallNonvirtualVoidMethodA", nonvirtual_Void_a);
    B("GetVersion", version);
    B("FindClass", find_class);
    B("GetSuperclass", superclass);
    B("IsAssignableFrom", assignable);
    B("NewGlobalRef", global_ref);
    B("NewLocalRef", local_ref);
    B("NewWeakGlobalRef", weak_ref);
    B("DeleteGlobalRef", delete_global);
    B("DeleteLocalRef", delete_local);
    B("DeleteWeakGlobalRef", delete_weak);
    B("IsSameObject", same);
    B("GetObjectRefType", ref_type);
    B("PushLocalFrame", push_frame);
    B("PopLocalFrame", pop_frame);
    B("EnsureLocalCapacity", ensure);
    B("AllocObject", allocate);
    B("NewObject", new_object);
    B("NewObjectV", new_object_v);
    B("NewObjectA", new_object_a);
    B("GetObjectClass", object_class);
    B("IsInstanceOf", instance);
    B("GetMethodID", method_id);
    B("GetStaticMethodID", static_method_id);
    B("GetFieldID", field_id);
    B("GetStaticFieldID", static_field_id);
    B("Throw", throw_object);
    B("ThrowNew", throw_new);
    B("ExceptionOccurred", exception_occurred);
    B("ExceptionDescribe", exception_describe);
    B("ExceptionClear", exception_clear);
    B("ExceptionCheck", exception_check);
    B("FatalError", fatal);
    B("NewString", new_string);
    B("NewStringUTF", new_utf);
    B("GetStringLength", string_length);
    B("GetStringUTFLength", utf_length);
    B("GetStringChars", string_chars);
    B("GetStringUTFChars", utf_chars);
    B("ReleaseStringChars", release_chars);
    B("ReleaseStringUTFChars", release_chars);
    B("GetStringCritical", string_chars);
    B("ReleaseStringCritical", release_chars);
    B("GetStringRegion", string_region);
    B("GetStringUTFRegion", utf_region);
    B("GetArrayLength", array_length);
    B("NewObjectArray", object_array_new);
    B("GetObjectArrayElement", array_object_get);
    B("SetObjectArrayElement", array_object_set);
    B("GetPrimitiveArrayCritical", array_elements);
    B("ReleasePrimitiveArrayCritical", array_release);
    B("NewDirectByteBuffer", direct_new);
    B("GetDirectBufferAddress", direct_address);
    B("GetDirectBufferCapacity", direct_capacity);
    B("MonitorEnter", monitor_enter);
    B("MonitorExit", monitor_exit);
    B("RegisterNatives", register_natives);
    B("GetJavaVM", get_vm);
#undef B
    vm_table_[4] = reinterpret_cast<uint32_t>(&jni::attach);
    vm_table_[5] = reinterpret_cast<uint32_t>(&jni::detach);
    vm_table_[6] = reinterpret_cast<uint32_t>(&get_env);
    vm_table_[7] = vm_table_[4];
    vm_ = {vm_table_, this};
    traps_.seal();
    attach();
    klass("java/lang/Object");
    klass("java/lang/Class");
    klass("java/lang/String");
}
Runtime::~Runtime() {
    detach();
    bionic::set_thread_cleanup(nullptr);
}
Environment *Runtime::env() { return current && current->runtime == this ? current.get() : nullptr; }
Environment *Runtime::attach() {
    if (auto *e = env())
        return e;
    current = std::make_unique<Environment>();
    current->functions = env_table_;
    current->runtime = this;
    return current.get();
}
int Runtime::detach() {
    auto *e = env();
    if (!e)
        return 0;
    for (auto &frame : e->frames)
        for (auto h : frame)
            release(h, 1);
    current.reset();
    return 0;
}
std::shared_ptr<Object> Runtime::object(Handle h) {
    if (!h)
        return {};
    std::lock_guard lock(guard_);
    auto it = references_.find(h);
    if (it == references_.end() || it->second->kind == 0)
        throw std::runtime_error("Invalid JNI reference");
    return it->second->kind == 3 ? it->second->weak.lock() : it->second->value;
}
Handle Runtime::reference(std::shared_ptr<Object> value, int kind) {
    if (!value)
        return nullptr;
    auto ref = std::make_unique<Reference>();
    ref->kind = kind;
    if (kind == 3)
        ref->weak = value;
    else
        ref->value = std::move(value);
    Handle h = ref.get();
    {
        std::lock_guard lock(guard_);
        references_[h] = std::move(ref);
    }
    if (kind == 1)
        attach()->frames.back().push_back(h);
    return h;
}
void Runtime::release(Handle h, int kind) {
    if (!h)
        return;
    std::lock_guard lock(guard_);
    auto it = references_.find(h);
    if (it != references_.end() && it->second->kind == kind) {
        it->second->value.reset();
        it->second->weak.reset();
        it->second->kind = 0;
    }
}
int Runtime::reference_type(Handle h) {
    std::lock_guard lock(guard_);
    auto it = references_.find(h);
    return it == references_.end() ? 0 : it->second->kind;
}
size_t Runtime::live_references() {
    std::lock_guard lock(guard_);
    size_t n = 0;
    for (const auto &[h, ref] : references_) {
        (void)h;
        if (ref->kind)
            ++n;
    }
    return n;
}
std::shared_ptr<Object> Runtime::klass(const std::string &name) {
    std::lock_guard lock(guard_);
    auto &c = classes_[name];
    if (!c) {
        c = std::make_shared<Object>();
        c->kind = Object::Kind::klass;
        c->name = name;
    }
    return c;
}
Handle Runtime::find_class(const std::string &name) {
    {
        std::lock_guard lock(guard_);
        if (!classes_.contains(name))
            bionic::log_once("JNI automatic class: " + name);
    }
    return reference(klass(name));
}
Handle Runtime::string(const std::string &utf) {
    auto text = decode(utf.c_str());
    return string16(text.data(), text.size());
}
Handle Runtime::string16(const char16_t *data, size_t n) {
    auto o = std::make_shared<Object>();
    o->kind = Object::Kind::string;
    o->klass = klass("java/lang/String");
    if (n)
        o->text.assign(data, data + n);
    o->utf = encode(o->text);
    return reference(o);
}
std::string Runtime::text(Handle h) {
    auto o = object(h);
    return o ? o->utf : "";
}
Handle Runtime::bytes(const void *data, size_t n) {
    auto h = array_new(attach(), static_cast<int>(n), 1, 'B');
    if (n)
        std::memcpy(object(h)->bytes.data(), data, n);
    return h;
}
Method *Runtime::method(Handle c, const char *name, const char *sig, bool is_static) {
    auto cls = object(c);
    if (!cls || !name || !sig)
        return nullptr;
    std::string key = cls->name + "." + name + sig + (is_static ? " static" : " instance");
    std::lock_guard lock(guard_);
    auto &m = methods_[key];
    if (!m) {
        m = std::make_unique<Method>();
        m->owner = cls->name;
        m->name = name;
        m->signature = sig;
        m->is_static = is_static;
    }
    return m.get();
}
Field *Runtime::field(Handle c, const char *name, const char *sig, bool is_static) {
    auto cls = object(c);
    if (!cls || !name || !sig)
        return nullptr;
    std::string key = cls->name + "." + name + ":" + sig + (is_static ? " static" : " instance");
    std::lock_guard lock(guard_);
    auto &f = fields_[key];
    if (!f) {
        f = std::make_unique<Field>();
        f->owner = cls->name;
        f->name = name;
        f->signature = sig;
        f->is_static = is_static;
    }
    return f.get();
}
void Runtime::register_method(const std::string &c, const std::string &n, const std::string &s,
                              Callback callback, bool stat) {
    auto h = reference(klass(c));
    auto *m = method(h, n.c_str(), s.c_str(), stat);
    m->callback = std::move(callback);
    release(h, 1);
}
void Runtime::register_field(const std::string &c, const std::string &n, const std::string &s, Value value,
                             bool stat) {
    auto h = reference(klass(c));
    auto *f = field(h, n.c_str(), s.c_str(), stat);
    if (s[0] == 'L' || s[0] == '[')
        f->object = object(value.l);
    else
        f->value = value;
    release(h, 1);
}
Value Runtime::invoke(Handle receiver, Method *m, const std::vector<Value> &args) {
    if (!m) {
        exception("java/lang/NoSuchMethodError", "null method ID");
        return {};
    }
    if (m->callback)
        return m->callback(*this, receiver, args);
    if (m->native) {
        auto *environment = attach();
        push_frame(environment, 16);
        std::vector<uint32_t> words = {reinterpret_cast<uint32_t>(attach()),
                                       reinterpret_cast<uint32_t>(receiver)};
        auto types = argument_types(m->signature);
        for (size_t i = 0; i < types.size(); ++i) {
            uint32_t pair[2];
            std::memcpy(pair, &args[i], 8);
            words.push_back(pair[0]);
            if (types[i] == 'J' || types[i] == 'D')
                words.push_back(pair[1]);
        }
        Value v;
        char return_type = m->signature[m->signature.find(')') + 1];
        if (return_type == 'J')
            v.j = static_cast<int64_t>(abi::call_guest_u64(m->native, words.data(), words.size()));
        else if (return_type == 'D')
            v.d = abi::call_guest_double(m->native, words.data(), words.size());
        else if (return_type == 'F')
            v.f = abi::call_guest_float(m->native, words.data(), words.size());
        else
            v.i = static_cast<int32_t>(abi::call_guest(m->native, words.data(), words.size()));
        Handle kept = pop_frame(environment, (return_type == 'L' || return_type == '[') ? v.l : nullptr);
        if (return_type == 'L' || return_type == '[')
            v.l = kept;
        return v;
    }
    bionic::log_once("JNI automatic method: " + m->owner + "." + m->name + m->signature);
    return {};
}
void Runtime::exception(const char *name, const char *message) {
    auto o = std::make_shared<Object>();
    o->klass = klass(name);
    o->name = message;
    attach()->exception = o;
}
uint32_t Runtime::native(const std::string &name, Handle receiver, const std::vector<uint32_t> &args) {
    uint32_t address = image_.lookup(name);
    if (!address)
        throw std::runtime_error("Missing native export: " + name);
    std::vector<uint32_t> words = {reinterpret_cast<uint32_t>(attach()),
                                   reinterpret_cast<uint32_t>(receiver)};
    words.insert(words.end(), args.begin(), args.end());
    std::printf("native %s\n", name.c_str());
    std::fflush(stdout);
    return abi::call_guest(address, words.data(), words.size());
}
} // namespace nova::jni
