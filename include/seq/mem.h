#ifndef SEQ_MEM_H
#define SEQ_MEM_H

#include <cstdint>
#include "stage.h"
#include "pipeline.h"
#include "var.h"
#include "array.h"
#include "types.h"

namespace seq {
	class SeqModule;
	class LoadStore;

	class Mem : public Stage {
	private:
		seq_int_t count;
	public:
		Mem(types::Type *type, seq_int_t count);
		void codegen(llvm::Module *module) override;
		void finalize(llvm::Module *module, llvm::ExecutionEngine *eng) override;
		static Mem& make(types::Type *type, seq_int_t count);
	};

	class LoadStore : public Stage {
	private:
		Var *ptr;
		Var *idx;
		seq_int_t constIdx;
		bool isStore;
	public:
		LoadStore(Var *ptr, Var *idx);
		LoadStore(Var *ptr, seq_int_t idx);
		void validate() override;
		void codegen(llvm::Module *module) override;

		Pipeline operator|(Pipeline to) override;

		static LoadStore& make(Var *ptr, Var *idx);
		static LoadStore& make(Var *ptr, seq_int_t idx);
	};
}

#endif /* SEQ_MEM_H */
