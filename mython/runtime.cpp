#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

    //////////////////////////////////////////////////////////////////////////////
    //
    //  ObjectHolder

    ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
        : data_(std::move(data)) {
    }

    void ObjectHolder::AssertIsValid() const {
        assert(data_ != nullptr);
    }

    ObjectHolder ObjectHolder::Share(Object& object) {
        // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
        return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
    }

    ObjectHolder ObjectHolder::None() {
        return {};
    }

    Object& ObjectHolder::operator*() const {
        AssertIsValid();
        return *Get();
    }

    Object* ObjectHolder::operator->() const {
        AssertIsValid();
        return Get();
    }

    Object* ObjectHolder::Get() const {
        return data_.get();
    }

    ObjectHolder::operator bool() const {
        return Get() != nullptr;
    }

    bool IsTrue(const ObjectHolder& object) {
        if (!object) {
            return false;
        }
        else {
            if (object.TryAs<Bool>()) {
                return object.TryAs<Bool>()->GetValue();
            }
            else if (object.TryAs<Number>()) {
                return object.TryAs<Number>()->GetValue() != 0;
            }
            else if (object.TryAs<String>()) {
                return !object.TryAs<String>()->GetValue().empty();
            }
            else if (object.TryAs<Class>()) {
                return false;
            }
            else if (object.TryAs<ClassInstance>()) {
                return false;
            }

        }
        return true;
    }

    //  ClassInstance

    ClassInstance::ClassInstance(const Class& cls)
        : cls_(cls) {
    }

    void ClassInstance::Print(std::ostream& os, Context& context) {
        const string STR_METHOD = "__str__"s;
        if (HasMethod(STR_METHOD, 0)) {
            Call(STR_METHOD, {}, context)->Print(os, context);
        }
        else {
            os << this;
        }
    }

    bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
        auto* p_method = cls_.GetMethod(method);
        return p_method && p_method->formal_params.size() == argument_count;
    }

    Closure& ClassInstance::Fields() {
        return closure_;
    }

    const Closure& ClassInstance::Fields() const {
        return closure_;
    }

    ObjectHolder ClassInstance::Call(const std::string& method,
        const std::vector<ObjectHolder>& actual_args,
        Context& context) {
        if (!HasMethod(method, actual_args.size())) {
            throw std::runtime_error("Method not found."s);
        }
        auto* p_method = cls_.GetMethod(method);
        Closure locals;
        locals["self"s] = ObjectHolder::Share(*this);
        for (size_t i = 0; i < p_method->formal_params.size(); ++i) {
            locals[p_method->formal_params.at(i)] = actual_args.at(i);
        }
        return p_method->body->Execute(locals, context);
    }

    //  Class

    Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
        : name_(std::move(name)), methods_(std::move(methods)), parent_(parent) {
        for (size_t i = 0; i < methods_.size(); ++i) {
            methods_by_name_[methods_.at(i).name] = i;
        }
    }

    const Method* Class::GetMethod(const std::string& name) const {
        if (methods_by_name_.count(name)) {
            return &methods_.at(methods_by_name_.at(name));
        }
        else if (parent_) {
            return parent_->GetMethod(name);
        }
        return nullptr;
    }

    [[nodiscard]] const std::string& Class::GetName() const {   // inline ?
        return name_;
    }

    void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
        os << "Class "sv << name_;
    }

    //  Bool

    void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
        os << (GetValue() ? "True"sv : "False"sv);
    }

    //  Globals

    bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        if (lhs.TryAs<Bool>() && rhs.TryAs<Bool>()) {
            return lhs.TryAs<Bool>()->GetValue() == rhs.TryAs<Bool>()->GetValue();
        }
        if (lhs.TryAs<Number>() && rhs.TryAs<Number>()) {
            return lhs.TryAs<Number>()->GetValue() == rhs.TryAs<Number>()->GetValue();
        }
        if (lhs.TryAs<String>() && rhs.TryAs<String>()) {
            return lhs.TryAs<String>()->GetValue() == rhs.TryAs<String>()->GetValue();
        }
        const string EQ_METHOD = "__eq__"s;
        if (lhs.TryAs<ClassInstance>() && lhs.TryAs<ClassInstance>()->HasMethod(EQ_METHOD, 1)) {
            return lhs.TryAs<ClassInstance>()->Call(EQ_METHOD, { rhs }, context).TryAs<Bool>()->GetValue();
        }
        if (!lhs.operator bool() && !rhs.operator bool()) {
            return true;
        }
        throw std::runtime_error("Cannot compare objects for equality"s);
    }

    bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        if (lhs.TryAs<Bool>() && rhs.TryAs<Bool>()) {
            return lhs.TryAs<Bool>()->GetValue() < rhs.TryAs<Bool>()->GetValue();
        }
        if (lhs.TryAs<Number>() && rhs.TryAs<Number>()) {
            return lhs.TryAs<Number>()->GetValue() < rhs.TryAs<Number>()->GetValue();
        }
        if (lhs.TryAs<String>() && rhs.TryAs<String>()) {
            return lhs.TryAs<String>()->GetValue() < rhs.TryAs<String>()->GetValue();
        }
        const string LT_METHOD = "__lt__"s;
        if (lhs.TryAs<ClassInstance>() && lhs.TryAs<ClassInstance>()->HasMethod(LT_METHOD, 1)) {
            return lhs.TryAs<ClassInstance>()->Call(LT_METHOD, { rhs }, context).TryAs<Bool>()->GetValue();
        }
        throw std::runtime_error("Cannot compare objects for less"s);
    }

    bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Equal(lhs, rhs, context);
    }

    bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
    }

    bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Greater(lhs, rhs, context);
    }

    bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Less(lhs, rhs, context);
    }

}  // namespace runtime
