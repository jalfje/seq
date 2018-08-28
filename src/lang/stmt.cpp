#include "seq/seq.h"

using namespace seq;
using namespace llvm;

Block::Block(Stmt *parent) :
    parent(parent), stmts()
{
}

void Block::add(Stmt *stmt)
{
	stmts.push_back(stmt);
	stmt->setParent(this);
}

void Block::resolveTypes()
{
	for (auto *stmt : stmts)
		stmt->resolveTypes();
}

void Block::codegen(BasicBlock*& block)
{
	for (auto *stmt : stmts)
		stmt->codegen(block);
}

Block *Block::clone(Generic *ref)
{
	if (ref->seenClone(this))
		return (Block *)ref->getClone(this);

	auto *x = new Block(parent ? parent->clone(ref) : nullptr);
	ref->addClone(this, x);

	std::vector<Stmt *> stmtsCloned;

	for (auto *stmt : stmts)
		stmtsCloned.push_back(stmt->clone(ref));

	x->stmts = stmtsCloned;
	return x;
}

Stmt::Stmt(std::string name) :
    base(nullptr), breaks(), continues(),
    parent(nullptr), loop(false), name(std::move(name))
{
}

std::string Stmt::getName() const
{
	return name;
}

Stmt *Stmt::getPrev() const
{
	if (!parent)
		return nullptr;

	return parent->parent;
}

void Stmt::setParent(Block *parent)
{
	assert(!this->parent);
	this->parent = parent;
}

BaseFunc *Stmt::getBase() const
{
	return base;
}

void Stmt::setBase(BaseFunc *base)
{
	if (!base)
		return;

	this->base = base;
}

static Stmt *findEnclosingLoop(Stmt *stmt)
{
	while (stmt) {
		if (stmt->isLoop())
			return stmt;
		stmt = stmt->getPrev();
	}

	throw exc::SeqException("break or continue outside of loop");
}

void Stmt::addBreakToEnclosingLoop(BranchInst *inst)
{
	findEnclosingLoop(this)->addBreak(inst);
}

void Stmt::addContinueToEnclosingLoop(BranchInst *inst)
{
	findEnclosingLoop(this)->addContinue(inst);
}

bool Stmt::isLoop()
{
	return loop;
}

void Stmt::ensureLoop()
{
	assert(isLoop());
}

void Stmt::addBreak(BranchInst *inst)
{
	ensureLoop();
	breaks.push_back(inst);
}

void Stmt::addContinue(BranchInst *inst)
{
	ensureLoop();
	continues.push_back(inst);
}

void Stmt::setBreaks(BasicBlock *block)
{
	ensureLoop();
	for (auto *inst : breaks)
		inst->setSuccessor(0, block);
}

void Stmt::setContinues(BasicBlock *block)
{
	ensureLoop();
	for (auto *inst : continues)
		inst->setSuccessor(0, block);
}

void Stmt::resolveTypes()
{
}

void Stmt::codegen(BasicBlock*& block)
{
	throw exc::SeqException("cannot codegen abstract statement");
}

void Stmt::setCloneBase(Stmt *stmt, Generic *ref)
{
	if (base) stmt->base = base->clone(ref);
	if (parent) stmt->parent = parent->clone(ref);
	stmt->loop = loop;
	stmt->name = name;
}

Stmt *Stmt::clone(Generic *ref)
{
	throw exc::SeqException("cannot clone '" + getName() + "' statement");
}

std::ostream& operator<<(std::ostream& os, Stmt& stmt)
{
	return os << stmt.getName();
}
