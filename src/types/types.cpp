#include <cstdlib>
#include <iostream>
#include <vector>
#include <typeinfo>
#include "seq/seq.h"

using namespace seq;
using namespace llvm;

types::Type::Type(std::string name, types::Type *parent, bool abstract) :
    name(std::move(name)), parent(parent), abstract(abstract)
{
}

std::string types::Type::getName() const
{
	return name;
}

types::Type *types::Type::getParent() const
{
	return parent;
}

bool types::Type::isAbstract() const
{
	return abstract;
}

types::VTable& types::Type::getVTable()
{
	return vtable;
}

Value *types::Type::alloc(Value *count, BasicBlock *block)
{
	if (size(block->getModule()) == 0)
		throw exc::SeqException("cannot create array of type '" + getName() + "'");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	auto *allocFunc = cast<Function>(
	                    module->getOrInsertFunction(
	                      allocFuncName(),
	                      IntegerType::getInt8PtrTy(context),
	                      IntegerType::getIntNTy(context, sizeof(size_t)*8)));

	IRBuilder<> builder(block);

	Value *elemSize = ConstantInt::get(seqIntLLVM(context), (uint64_t)size(block->getModule()));
	Value *fullSize = builder.CreateMul(count, elemSize);
	fullSize = builder.CreateBitCast(fullSize, IntegerType::getIntNTy(context, sizeof(size_t)*8));
	Value *mem = builder.CreateCall(allocFunc, {fullSize});
	return builder.CreatePointerCast(mem, PointerType::get(getLLVMType(context), 0));
}

Value *types::Type::alloc(seq_int_t count, BasicBlock *block)
{
	LLVMContext& context = block->getContext();
	return alloc(ConstantInt::get(seqIntLLVM(context), (uint64_t)count, true), block);
}

Value *types::Type::call(BaseFunc *base,
                         Value *self,
                         const std::vector<Value *>& args,
                         BasicBlock *block)
{
	throw exc::SeqException("cannot call type '" + getName() + "'");
}

static Value *funcAsMethod(types::Type *type,
                           BaseFunc *method,
                           Value *self,
                           BasicBlock *block)
{
	FuncExpr e(method);
	auto *funcType = dynamic_cast<types::FuncType *>(e.getType());
	assert(funcType);
	Value *func = e.codegen(nullptr, block);
	return types::MethodType::get(type, funcType)->make(self, func, block);
}

static types::MethodType *funcAsMethodType(types::Type *type, BaseFunc *method)
{
	FuncExpr e(method);
	auto *funcType = dynamic_cast<types::FuncType *>(e.getType());
	assert(funcType);
	return types::MethodType::get(type, funcType);
}

static Value *funcAsStaticMethod(BaseFunc *method, BasicBlock *block)
{
	FuncExpr e(method);
	Value *func = e.codegen(nullptr, block);
	return func;
}

static types::FuncType *funcAsStaticMethodType(BaseFunc *method)
{
	FuncExpr e(method);
	auto *funcType = dynamic_cast<types::FuncType *>(e.getType());
	assert(funcType);
	return funcType;
}

Value *types::Type::memb(Value *self,
                         const std::string& name,
                         BasicBlock *block)
{
	initFields();
	initOps();

	for (auto& magic : vtable.overloads) {
		if (magic.name == name)
			return funcAsMethod(this, magic.func, self, block);
	}

	for (auto& magic : vtable.magic) {
		if (name == magic.name)
			return funcAsMethod(this, magic.asFunc(this), self, block);
	}

	auto iter1 = getVTable().methods.find(name);
	if (iter1 != getVTable().methods.end())
		return funcAsMethod(this, iter1->second, self, block);

	auto iter2 = getVTable().fields.find(name);
	if (iter2 == getVTable().fields.end())
		throw exc::SeqException("type '" + getName() + "' has no member '" + name + "'");

	IRBuilder<> builder(block);
	return builder.CreateExtractValue(self, iter2->second.first);
}

types::Type *types::Type::membType(const std::string& name)
{
	initFields();
	initOps();

	for (auto& magic : vtable.overloads) {
		if (magic.name == name)
			return funcAsMethodType(this, magic.func);
	}

	for (auto& magic : vtable.magic) {
		if (name == magic.name)
			return funcAsMethodType(this, magic.asFunc(this));
	}

	auto iter1 = getVTable().methods.find(name);
	if (iter1 != getVTable().methods.end())
		return funcAsMethodType(this, iter1->second);

	auto iter2 = getVTable().fields.find(name);
	if (iter2 == getVTable().fields.end() || iter2->second.second->is(types::Void))
		throw exc::SeqException("type '" + getName() + "' has no member '" + name + "'");

	return iter2->second.second;
}

Value *types::Type::staticMemb(const std::string& name, BasicBlock *block)
{
	initOps();

	for (auto& magic : vtable.overloads) {
		if (magic.name == name)
			return funcAsStaticMethod(magic.func, block);
	}

	for (auto& magic : vtable.magic) {
		if (name == magic.name)
			return funcAsStaticMethod(magic.asFunc(this), block);
	}

	auto iter1 = getVTable().methods.find(name);
	if (iter1 != getVTable().methods.end())
		return funcAsStaticMethod(iter1->second, block);

	throw exc::SeqException("type '" + getName() + "' has no static member '" + name + "'");
}

types::Type *types::Type::staticMembType(const std::string& name)
{
	initOps();

	for (auto& magic : vtable.overloads) {
		if (magic.name == name)
			return funcAsStaticMethodType(magic.func);
	}

	for (auto& magic : vtable.magic) {
		if (name == magic.name)
			return funcAsStaticMethodType(magic.asFunc(this));
	}

	auto iter1 = getVTable().methods.find(name);
	if (iter1 != getVTable().methods.end())
		return funcAsStaticMethodType(iter1->second);

	throw exc::SeqException("type '" + getName() + "' has no static member '" + name + "'");
}

Value *types::Type::setMemb(Value *self,
                            const std::string& name,
                            Value *val,
                            BasicBlock *block)
{
	initFields();
	auto iter = getVTable().fields.find(name);

	if (iter == getVTable().fields.end())
		throw exc::SeqException("type '" + getName() + "' has no assignable member '" + name + "'");

	IRBuilder<> builder(block);
	return builder.CreateInsertValue(self, val, iter->second.first);
}

bool types::Type::hasMethod(const std::string& name)
{
	for (auto& magic : vtable.overloads) {
		if (magic.name == name)
			return true;
	}

	for (auto& magic : vtable.magic) {
		if (name == magic.name)
			return true;
	}

	return getVTable().methods.find(name) != getVTable().methods.end();
}

static bool isMagic(const std::string& name)
{
	return name.size() >= 4 &&
	       name[0] == '_' &&
	       name[1] == '_' &&
	       name[name.size() - 1] == '_' &&
	       name[name.size() - 2] == '_';
}

void types::Type::addMethod(std::string name, BaseFunc *func, bool force)
{
	if (isMagic(name)) {
		if (name == "__new__")
			throw exc::SeqException("cannot override __new__");

		// insert at the start so we always find the latest-added alternative first
		vtable.overloads.insert(vtable.overloads.begin(), {name, func});
		return;
	}

	if (hasMethod(name)) {
		if (force) {
			getVTable().methods[name] = func;
			return;
		} else {
			throw exc::SeqException("duplicate method '" + name + "'");
		}
	}

	if (getVTable().fields.find(name) != getVTable().fields.end())
		throw exc::SeqException("field '" + name + "' conflicts with method");

	getVTable().methods.insert({name, func});
}

BaseFunc *types::Type::getMethod(const std::string& name)
{
	auto iter = getVTable().methods.find(name);

	if (iter == getVTable().methods.end())
		throw exc::SeqException("type '" + getName() + "' has no method '" + name + "'");

	return iter->second;
}

Value *types::Type::defaultValue(BasicBlock *block)
{
	throw exc::SeqException("type '" + getName() + "' has no default value");
}

Value *types::Type::boolValue(Value *self, BasicBlock *block)
{
	if (!magicOut("__bool__", {})->is(types::Bool))
		throw exc::SeqException("the output type of __bool__ is not boolean");

	return callMagic("__bool__", {}, self, {}, block);
}

void types::Type::initOps()
{
}

void types::Type::initFields()
{
}

static std::string argsVecToStr(const std::vector<types::Type *>& args)
{
	if (args.empty())
		return "()";

	std::string result = "(" + args[0]->getName();
	for (unsigned i = 1; i< args.size(); i++)
		result += ", " + args[i]->getName();

	result += ")";
	return result;
}

types::Type *types::Type::magicOut(const std::string& name, std::vector<types::Type *> args)
{
	initOps();

	args.insert(args.begin(), this);
	for (auto& magic : vtable.overloads) {
		if (magic.name != name)
			continue;

		try {
			std::vector<Expr *> argExprs;
			for (auto *arg : args)
				argExprs.push_back(new ValueExpr(arg, nullptr));

			FuncExpr func(magic.func);
			CallExpr call(&func, argExprs);
			call.resolveTypes();
			return call.getType();
		} catch (exc::SeqException& s) {
			// maybe a later method will match our argument types, so continue
		}
	}
	args.erase(args.begin());

	for (auto& magic : vtable.magic) {
		if (name == magic.name && typeMatch<>(args, magic.args))
			return magic.out;
	}

	throw exc::SeqException("cannot find method '" + name + "' for type '" + getName() + "' with specified argument types " + argsVecToStr(args));
}

Value *types::Type::callMagic(const std::string& name,
                              std::vector<types::Type *> argTypes,
                              Value *self,
                              std::vector<Value *> args,
                              BasicBlock *block)
{
	initOps();

	argTypes.insert(argTypes.begin(), this);
	args.insert(args.begin(), self);
	for (auto& magic : vtable.overloads) {
		if (magic.name != name)
			continue;

		try {
			std::vector<Expr *> argExprs;
			assert(argTypes.size() == args.size());
			for (unsigned i = 0; i < args.size(); i++)
				argExprs.push_back(new ValueExpr(argTypes[i], args[i]));

			FuncExpr func(magic.func);
			CallExpr call(&func, argExprs);
			call.resolveTypes();
			return call.codegen(nullptr, block);
		} catch (exc::SeqException&) {
			// maybe a later method will match our argument types, so continue
		}
	}
	argTypes.erase(argTypes.begin());
	args.erase(args.begin());

	for (auto& magic : vtable.magic) {
		if (name == magic.name && typeMatch<>(argTypes, magic.args)) {
			IRBuilder<> builder(block);
			return magic.codegen(self, args, builder);
		}
	}

	throw exc::SeqException("cannot find method '" + name + "' for type '" + getName() + "' with specified argument types " + argsVecToStr(argTypes));
}

bool types::Type::isAtomic() const
{
	return true;
}

bool types::Type::is(types::Type *type) const
{
	return isGeneric(type);
}

bool types::Type::isGeneric(types::Type *type) const
{
	return typeid(*this) == typeid(*type);
}

unsigned types::Type::numBaseTypes() const
{
	return 0;
}

types::Type *types::Type::getBaseType(unsigned idx) const
{
	throw exc::SeqException("type '" + getName() + "' has no base types");
}

types::Type *types::Type::getCallType(const std::vector<Type *>& inTypes)
{
	throw exc::SeqException("cannot call type '" + getName() + "'");
}

Type *types::Type::getLLVMType(LLVMContext& context) const
{
	throw exc::SeqException("cannot instantiate '" + getName() + "' class");
}

seq_int_t types::Type::size(Module *module) const
{
	return 0;
}

types::RecordType *types::Type::asRec()
{
	return nullptr;
}

types::RefType *types::Type::asRef()
{
	return nullptr;
}

types::GenType *types::Type::asGen()
{
	return nullptr;
}

types::OptionalType *types::Type::asOpt()
{
	return nullptr;
}

types::Type *types::Type::clone(Generic *ref)
{
	return this;
}

BaseFunc *MagicMethod::asFunc(types::Type *type) const
{
	std::vector<types::Type *> argsFull(args);
	argsFull.insert(argsFull.begin(), type);

	return new BaseFuncLite(argsFull, out, [this,type](Module *module) {
		LLVMContext& context = module->getContext();
		std::vector<Type *> types;
		types.push_back(type->getLLVMType(context));

		for (auto *arg : args)
			types.push_back(arg->getLLVMType(context));

		static int idx = 1;
		auto *func = cast<Function>(module->getOrInsertFunction("seq.magic." + name + "." + std::to_string(idx++),
		                                                        FunctionType::get(out->getLLVMType(context), types, false)));

		BasicBlock *entry = BasicBlock::Create(context, "entry", func);

		std::vector<Value *> args;
		for (auto& arg : func->args())
			args.push_back(&arg);

		Value *self = nullptr;
		if (!args.empty()) {
			self = args[0];
			args.erase(args.begin());
		}

		IRBuilder<> builder(entry);
		Value *result = codegen(self, args, builder);
		if (result)
			builder.CreateRet(result);
		else
			builder.CreateRetVoid();

		return func;
	});
}

bool types::is(types::Type *type1, types::Type *type2)
{
	return type1->is(type2) || type2->is(type1);
}
