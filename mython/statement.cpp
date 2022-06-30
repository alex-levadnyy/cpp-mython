#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast
{

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace
{
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
} // namespace

ObjectHolder Assignment::Execute(Closure &closure, Context &context)
{
    closure[var_] = rv_->Execute(closure, context);
    return closure.at(var_);
}

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
    : var_(std::move(var)), rv_(std::move(rv))
{
}

VariableValue::VariableValue(const std::string &var_name) : var_name_(var_name) {}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
{
    if (dotted_ids.size() == 0)
    {
        return;
    }
    var_name_ = std::move(dotted_ids[0]);
    dotted_ids_.reserve(dotted_ids.size() - 1);
    for (size_t i = 1; i < dotted_ids.size(); i++)
    {
        dotted_ids_.push_back(std::move(dotted_ids[i]));
    }
}

ObjectHolder VariableValue::Execute(Closure &closure, [[maybe_unused]] Context &context)
{
    auto iter = closure.find(var_name_);
    if (iter != closure.end())
    {
        ObjectHolder current_object = iter->second;
        for (size_t i = 0; i < dotted_ids_.size(); i++)
        {
            runtime::ClassInstance *current_ptr = current_object.TryAs<runtime::ClassInstance>();
            if (current_ptr != nullptr)
            {
                Closure &fields = current_ptr->Fields();
                current_object = fields.at(dotted_ids_[i]);
            }
        }
        return current_object;
    }
    else
    {
        throw std::runtime_error("Failed to VariableValue::Execute!");
    }
}

unique_ptr<Print> Print::Variable(const std::string &name)
{
    return make_unique<Print>(make_unique<VariableValue>(name));
}

Print::Print(unique_ptr<Statement> argument)
{
    args_.push_back(std::move(argument));
}

Print::Print(vector<unique_ptr<Statement>> args) : args_(std::move(args)) {}

ObjectHolder Print::Execute(Closure &closure, Context &context)
{
    bool is_first = true;
    std::ostream &cur_output_stream = context.GetOutputStream();
    for (const auto &arg : args_)
    {
        if (!is_first)
        {
            cur_output_stream << ' ';
        }
        is_first = false;
        ObjectHolder object = arg->Execute(closure, context);
        if (object.Get() != nullptr)
        {
            object.Get()->Print(cur_output_stream, context);
        }
        else
        {
            cur_output_stream << "None";
        }
    }
    cur_output_stream << '\n';
    return {};
}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args)
    : object_(std::move(object)), method_(std::move(method)), args_(std::move(args))
{
}

ObjectHolder MethodCall::Execute(Closure &closure, Context &context)
{
    ObjectHolder current_object = object_->Execute(closure, context);
    auto class_ptr = current_object.TryAs<runtime::ClassInstance>();
    if (class_ptr != nullptr)
    {
        std::vector<ObjectHolder> method_args;
        method_args.reserve(args_.size());
        for (size_t i = 0; i < args_.size(); i++)
        {
            method_args.push_back(args_[i]->Execute(closure, context));
        }
        return class_ptr->Call(method_, method_args, context);
    }
    return ObjectHolder::None();
}

ObjectHolder Stringify::Execute(Closure &closure, Context &context)
{
    auto executed_arg = argument_->Execute(closure, context);
    std::ostringstream result_str;
    if (executed_arg.Get() != nullptr)
    {
        executed_arg->Print(result_str, context);
        return ObjectHolder::Own(runtime::String(result_str.str()));
    }
    else
        return ObjectHolder::Own(runtime::String("None"));
}

ObjectHolder Add::Execute(Closure &closure, Context &context)
{
    ObjectHolder lhs = lhs_->Execute(closure, context);
    ObjectHolder rhs = rhs_->Execute(closure, context);

    auto class_ptr = lhs.TryAs<runtime::ClassInstance>();
    if (class_ptr != nullptr)
    {
        if (class_ptr->HasMethod(ADD_METHOD, 1U))
        {
            return class_ptr->Call(ADD_METHOD, {rhs}, context);
        }
    }

#define ADDABLE_VALUE(type)                                                                                  \
    {                                                                                                        \
        auto lhs_ptr = lhs.TryAs<type>();                                                                    \
        auto rhs_ptr = rhs.TryAs<type>();                                                                    \
        if (lhs_ptr != nullptr && rhs_ptr != nullptr)                                                        \
        {                                                                                                    \
            return ObjectHolder::Own(type(lhs_ptr->GetValue() + rhs_ptr->GetValue()));                       \
        }                                                                                                    \
    }

    ADDABLE_VALUE(runtime::String);
    ADDABLE_VALUE(runtime::Number);

#undef ADDABLE_VALUE

    throw std::runtime_error("Failed to Add::Execute ");
}

ObjectHolder Sub::Execute(Closure &closure, Context &context)
{
    ObjectHolder lhs = lhs_->Execute(closure, context);
    ObjectHolder rhs = rhs_->Execute(closure, context);
    auto number_lhs_ptr = lhs.TryAs<runtime::Number>();
    auto number_rhs_ptr = rhs.TryAs<runtime::Number>();

    if (number_lhs_ptr != nullptr && number_rhs_ptr != nullptr)
    {
        return ObjectHolder::Own(runtime::Number(number_lhs_ptr->GetValue() - number_rhs_ptr->GetValue()));
    }

    throw std::runtime_error("Failed to Sub::Execute ");
}

ObjectHolder Mult::Execute(Closure &closure, Context &context)
{
    ObjectHolder lhs = lhs_->Execute(closure, context);
    ObjectHolder rhs = rhs_->Execute(closure, context);
    auto number_lhs_ptr = lhs.TryAs<runtime::Number>();
    auto number_rhs_ptr = rhs.TryAs<runtime::Number>();

    if (number_lhs_ptr != nullptr && number_rhs_ptr != nullptr)
    {
        return ObjectHolder::Own(runtime::Number(number_lhs_ptr->GetValue() * number_rhs_ptr->GetValue()));
    }

    throw std::runtime_error("Failed to Mult::Execute ");
}

ObjectHolder Div::Execute(Closure &closure, Context &context)
{
    ObjectHolder lhs = lhs_->Execute(closure, context);
    ObjectHolder rhs = rhs_->Execute(closure, context);
    auto number_lhs_ptr = lhs.TryAs<runtime::Number>();
    auto number_rhs_ptr = rhs.TryAs<runtime::Number>();

    if (number_lhs_ptr != nullptr && number_rhs_ptr != nullptr)
    {
        if (number_rhs_ptr->GetValue() != 0)
        {
            int result = number_lhs_ptr->GetValue() / number_rhs_ptr->GetValue();
            return ObjectHolder::Own(runtime::Number(result));
        }
        else
        {
            throw std::runtime_error("Сannot be divided by 0 ");
        }
    }
    else
    {
        throw std::runtime_error("Failed to Div::Execute ");
    }
}

ObjectHolder Compound::Execute(Closure &closure, Context &context)
{
    for (const auto &arg : args_)
    {
        arg->Execute(closure, context);
    }
    return {};
}

ObjectHolder Return::Execute(Closure &closure, Context &context)
{
    throw statement_->Execute(closure, context);
}

ClassDefinition::ClassDefinition(ObjectHolder cls) : cls_(std::move(cls)) {}

ObjectHolder ClassDefinition::Execute(Closure &closure, [[maybe_unused]] Context &context)
{
    closure[cls_.TryAs<runtime::Class>()->GetName()] = cls_;
    return {};
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name, std::unique_ptr<Statement> rv)
    : object_(std::move(object)), field_name_(std::move(field_name)), rv_(std::move(rv))
{
}

ObjectHolder FieldAssignment::Execute(Closure &closure, Context &context)
{
    auto class_ptr = object_.Execute(closure, context).TryAs<runtime::ClassInstance>();
    if (class_ptr != nullptr)
    {
        Closure &fields = class_ptr->Fields();
        fields[field_name_] = rv_->Execute(closure, context);
        return fields.at(field_name_);
    }
    else
    {
        throw std::runtime_error("Failed to FieldAssignment::Execute ");
    }
}

IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body)
    : condition_(std::move(condition)), if_body_(std::move(if_body)), else_body_(std::move(else_body))
{
}

ObjectHolder IfElse::Execute(Closure &closure, Context &context)
{
    if (runtime::IsTrue(condition_->Execute(closure, context)))
    {
        return if_body_->Execute(closure, context);
    }
    else if (else_body_ != nullptr)
    {
        return else_body_->Execute(closure, context);
    }
    return {};
}

ObjectHolder Or::Execute(Closure &closure, Context &context)
{
    if (runtime::IsTrue(lhs_->Execute(closure, context)))
    {
        return ObjectHolder::Own(runtime::Bool(true));
    }
    else if (runtime::IsTrue(rhs_->Execute(closure, context)))
    {
        return ObjectHolder::Own(runtime::Bool(true));
    }
    return ObjectHolder::Own(runtime::Bool(false));
}

ObjectHolder And::Execute(Closure &closure, Context &context)
{
    if (runtime::IsTrue(lhs_->Execute(closure, context)))
    {
        if (runtime::IsTrue(rhs_->Execute(closure, context)))
        {
            return ObjectHolder::Own(runtime::Bool(true));
        }
    }
    return ObjectHolder::Own(runtime::Bool(false));
}

ObjectHolder Not::Execute(Closure &closure, Context &context)
{
    bool result = !runtime::IsTrue(argument_->Execute(closure, context));
    return ObjectHolder::Own(runtime::Bool(result));
}

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs)), comparator_(std::move(cmp))
{
}

ObjectHolder Comparison::Execute(Closure &closure, Context &context)
{
    return ObjectHolder::Own(runtime::Bool(
        comparator_(lhs_->Execute(closure, context), rhs_->Execute(closure, context), context)));
}

NewInstance::NewInstance(const runtime::Class &_class, std::vector<std::unique_ptr<Statement>> args)
    : class_instance_(_class), args_(std::move(args))
{
}

NewInstance::NewInstance(const runtime::Class &_class) : class_instance_(_class) {}

ObjectHolder NewInstance::Execute(Closure &closure, Context &context)
{
    if (class_instance_.HasMethod(INIT_METHOD, args_.size()))
    {
        std::vector<ObjectHolder> tmp_args;
        tmp_args.reserve(args_.size());
        for (const auto &arg : args_)
        {
            tmp_args.push_back(arg->Execute(closure, context));
        }
        class_instance_.Call(INIT_METHOD, tmp_args, context);
    }
    return ObjectHolder::Share(class_instance_);
}

MethodBody::MethodBody(std::unique_ptr<Statement> &&body) : body_(std::move(body)) {}

ObjectHolder MethodBody::Execute(Closure &closure, Context &context)
{
    try
    {
        return body_->Execute(closure, context);
    }
    catch (ObjectHolder &obj)
    {
        return obj;
    }
    catch (...)
    {
        throw std::runtime_error("MethodBody::Catched undefined object");
    }
}

} // namespace ast