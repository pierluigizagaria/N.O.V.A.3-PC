#pragma once
#include "loader/elf_image.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace nova::jni {
using Handle = void *;
#pragma pack(push, 4)
union Value {
    uint8_t z;
    int8_t b;
    uint16_t c;
    int16_t s;
    int32_t i;
    int64_t j;
    float f;
    double d;
    Handle l;
    Value() : j(0) {}
};
#pragma pack(pop)
static_assert(sizeof(Value) == 8 && sizeof(Handle) == 4);
struct Object;
struct Method;
struct Field;
class Runtime;
struct Environment {
    uint32_t *functions;
    Runtime *runtime;
    std::vector<std::vector<Handle>> frames{{}};
    std::shared_ptr<Object> exception;
};
struct VM {
    uint32_t *functions;
    Runtime *runtime;
};
using Callback = std::function<Value(Runtime &, Handle, const std::vector<Value> &)>;
struct Object {
    enum class Kind { object, klass, string, array, object_array, direct };
    Kind kind = Kind::object;
    std::string name;
    std::shared_ptr<Object> klass;
    std::u16string text;
    std::string utf;
    std::vector<uint8_t> bytes;
    std::vector<std::shared_ptr<Object>> objects;
    int32_t length = 0;
    size_t element_size = 1;
    char element_type = 'B';
    std::unordered_map<Field *, Value> fields;
    std::unordered_map<Field *, std::shared_ptr<Object>> object_fields;
    void *direct = nullptr;
    int64_t capacity = 0;
    std::recursive_mutex monitor;
};
struct Method {
    std::string owner, name, signature;
    bool is_static = false;
    Callback callback;
    uint32_t native = 0;
};
struct Field {
    std::string owner, name, signature;
    bool is_static = false;
    Value value;
    std::shared_ptr<Object> object;
};
class Runtime {
  public:
    explicit Runtime(const ElfImage &image);
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    VM *vm() { return &vm_; }
    Environment *env();
    Environment *attach();
    int detach();
    std::shared_ptr<Object> object(Handle handle);
    Handle reference(std::shared_ptr<Object> object, int kind = 1);
    void release(Handle handle, int kind);
    int reference_type(Handle handle);
    std::shared_ptr<Object> klass(const std::string &name);
    Handle find_class(const std::string &name);
    Handle string(const std::string &utf);
    Handle string16(const char16_t *data, size_t length);
    std::string text(Handle string);
    Handle bytes(const void *data, size_t length);
    Method *method(Handle klass, const char *name, const char *signature, bool is_static);
    Field *field(Handle klass, const char *name, const char *signature, bool is_static);
    void register_method(const std::string &klass, const std::string &name, const std::string &signature,
                         Callback callback, bool is_static = true);
    void register_field(const std::string &klass, const std::string &name, const std::string &signature,
                        Value value, bool is_static = true);
    Value invoke(Handle receiver, Method *method, const std::vector<Value> &arguments);
    void exception(const char *name, const char *message);
    uint32_t native(const std::string &name, Handle receiver, const std::vector<uint32_t> &args = {});
    const ElfImage &image() const { return image_; }
    size_t live_references();

  private:
    struct Reference {
        std::shared_ptr<Object> value;
        std::weak_ptr<Object> weak;
        int kind;
    };
    const ElfImage &image_;
    TrapStubs traps_;
    uint32_t env_table_[233]{}, vm_table_[8]{};
    VM vm_{};
    std::recursive_mutex guard_;
    std::unordered_map<Handle, std::unique_ptr<Reference>> references_;
    std::unordered_map<std::string, std::shared_ptr<Object>> classes_;
    std::unordered_map<std::string, std::unique_ptr<Method>> methods_;
    std::unordered_map<std::string, std::unique_ptr<Field>> fields_;
};
} // namespace nova::jni
