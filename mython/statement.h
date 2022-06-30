#pragma once

#include "runtime.h"

#include <functional>

namespace ast
{

using Statement = runtime::Executable;

template <typename T>
class ValueStatement : public Statement
{
public:
    explicit ValueStatement(T v) : value_(std::move(v)) {}

    runtime::ObjectHolder Execute(runtime::Closure & /*closure*/, runtime::Context & /*context*/) override
    {
        return runtime::ObjectHolder::Share(value_);
    }

private:
    T value_;
};

using NumericConst = ValueStatement<runtime::Number>;
using StringConst = ValueStatement<runtime::String>;
using BoolConst = ValueStatement<runtime::Bool>;


/*
Вычисляет значение переменной либо цепочки вызовов полей объектов id1.id2.id3.
*/
class VariableValue : public Statement
{
public:
    explicit VariableValue(const std::string &var_name);
    explicit VariableValue(std::vector<std::string> dotted_ids);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::string var_name_;
    std::vector<std::string> dotted_ids_;
};

// Присваивает переменной, имя которой задано в параметре var, значение выражения rv
class Assignment : public Statement
{
public:
    Assignment(std::string var, std::unique_ptr<Statement> rv);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::string var_;
    std::unique_ptr<Statement> rv_;
};

// Присваивает полю object.field_name значение выражения rv
class FieldAssignment : public Statement
{
public:
    FieldAssignment(VariableValue object, std::string field_name, std::unique_ptr<Statement> rv);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    VariableValue object_;
    std::string field_name_;
    std::unique_ptr<Statement> rv_;
};

class None : public Statement
{
public:
    runtime::ObjectHolder Execute([[maybe_unused]] runtime::Closure &closure,
                                  [[maybe_unused]] runtime::Context &context) override
    {
        return {};
    }
};

class Print : public Statement
{
public:
    explicit Print(std::unique_ptr<Statement> argument);

    explicit Print(std::vector<std::unique_ptr<Statement>> args);

    // Инициализирует команду print для вывода значения переменной name
    static std::unique_ptr<Print> Variable(const std::string &name);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::vector<std::unique_ptr<Statement>> args_;
};

// Вызывает метод object.method со списком параметров args
class MethodCall : public Statement
{
public:
    MethodCall(std::unique_ptr<Statement> object, std::string method,
               std::vector<std::unique_ptr<Statement>> args);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::unique_ptr<Statement> object_;
    std::string method_;
    std::vector<std::unique_ptr<Statement>> args_;
};

/*
Создаёт новый экземпляр класса class_, передавая его конструктору набор параметров args.
Если в классе отсутствует метод __init__ с заданным количеством аргументов,
то экземпляр класса создаётся без вызова конструктора (поля объекта не будут проинициализированы):
*/
class NewInstance : public Statement
{
public:
    explicit NewInstance(const runtime::Class &_class);

    NewInstance(const runtime::Class &_class, std::vector<std::unique_ptr<Statement>> args);
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    runtime::ClassInstance class_instance_;
    std::vector<std::unique_ptr<Statement>> args_;
};

class UnaryOperation : public Statement
{
public:
    explicit UnaryOperation(std::unique_ptr<Statement> argument) : argument_(std::move(argument)) {}

protected:
    std::unique_ptr<Statement> argument_;
};

// Операция str, возвращающая строковое значение своего аргумента
class Stringify : public UnaryOperation
{
public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

class BinaryOperation : public Statement
{
public:
    BinaryOperation(std::unique_ptr<Statement> lhs, std::unique_ptr<Statement> rhs)
        : lhs_(std::move(lhs)), rhs_(std::move(rhs))
    {
    }

protected:
    std::unique_ptr<Statement> lhs_;
    std::unique_ptr<Statement> rhs_;
};

class Add : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается сложение:
    //  число + число
    //  строка + строка
    //  объект1 + объект2, если у объект1 - пользовательский класс с методом _add__(rhs)
    // В противном случае при вычислении выбрасывается runtime_error
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

class Sub : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается вычитание:
    //  число - число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

class Mult : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается умножение:
    //  число * число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

class Div : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается деление:
    //  число / число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    // Если rhs равен 0, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

// Возвращает результат вычисления логической операции or над lhs и rhs
class Or : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

// Возвращает результат вычисления логической операции and над lhs и rhs
class And : public BinaryOperation
{
public:
    using BinaryOperation::BinaryOperation;

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

// Возвращает результат вычисления логической операции not над единственным аргументом операции
class Not : public UnaryOperation
{
public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;
};

// Составная инструкция (например: тело метода, содержимое ветки if, либо else)
class Compound : public Statement
{
public:
    template <typename... Args>
    explicit Compound(Args &&...args)
    {
        if constexpr (sizeof...(args) != 0)
        {
            CompoundImpl(args...);
        }
    }

    template <typename T0, typename... Args>
    void CompoundImpl(T0 &&head, Args &&...args)
    {
        args_.push_back(std::move(head));
        if constexpr (sizeof...(args) != 0)
        {
            CompoundImpl(args...);
        }
    }
    void AddStatement(std::unique_ptr<Statement> stmt)
    {
        args_.push_back(std::move(stmt));
    }

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::vector<std::unique_ptr<Statement>> args_;
};

// Тело метода. Как правило, содержит составную инструкцию
class MethodBody : public Statement
{
public:
    explicit MethodBody(std::unique_ptr<Statement> &&body);

    // Вычисляет инструкцию, переданную в качестве body.
    // Если внутри body была выполнена инструкция return, возвращает результат return
    // В противном случае возвращает None
    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::unique_ptr<Statement> body_;
};

class Return : public Statement
{
public:
    explicit Return(std::unique_ptr<Statement> statement) : statement_(std::move(statement)) {}

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::unique_ptr<Statement> statement_;
};

class ClassDefinition : public Statement
{
public:
    explicit ClassDefinition(runtime::ObjectHolder cls);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    runtime::ObjectHolder cls_;
};

class IfElse : public Statement
{
public:
    IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
           std::unique_ptr<Statement> else_body);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    std::unique_ptr<Statement> condition_;
    std::unique_ptr<Statement> if_body_;
    std::unique_ptr<Statement> else_body_;
};

class Comparison : public BinaryOperation
{
public:
    using Comparator =
        std::function<bool(const runtime::ObjectHolder &, const runtime::ObjectHolder &, runtime::Context &)>;

    Comparison(Comparator cmp, std::unique_ptr<Statement> lhs, std::unique_ptr<Statement> rhs);

    runtime::ObjectHolder Execute(runtime::Closure &closure, runtime::Context &context) override;

private:
    Comparator comparator_;
};

} // namespace ast