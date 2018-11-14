//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include<iostream>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace std;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   map<Decl*, int> mVars;
   map<Stmt*, int> mExprs;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, int val) {
   	// cout<< " bindDecl " << decl << " val " << val << endl;
		mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {
   	// cout<< "  getDeclVal " << decl << endl;
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(Stmt * stmt, int val) {
   	// cout<< " bindStmt " << stmt << "  val " << val << endl;
	   mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   // cout<< " getStmtVal " << stmt << "  val " << mExprs[stmt] << endl;
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }
};

/// Heap maps address to a value

class Heap {
	map<int, void*>mInt;
   map<int, int> mData;	
	map<void*, int>mAddr;
public:
	int Malloc(int size) {
		void * addr = malloc(size * sizeof(int));
		int buf = (unsigned long)addr;
		mAddr.insert(make_pair(addr, size));
		mInt.insert(make_pair(buf, addr));
		// cout<< "addr " << addr << " addr_int " << buf << endl;
      // Initialize the Content
      for (int i=0; i < size; ++i) {
      	mData.insert(std::make_pair(buf+i, 0));
      }
		return buf;
	}
	void Free(int address) {
		void * addr = mInt.find(address)->second;
		int size = mAddr.find(addr)->second;
		// cout << "free size " << size << endl;
		for(int i = 0; i < size; ++i) {

			mData.erase( address + i );
		}
		mInt.erase(address);
		free(addr);
		// cout << "mData " << (mData.find(address)->second) << endl;
	}
	void Update(int address, int val) {
   	mData[address] = val;
	}
	int get(int address) {
		if (mData.find(address) != mData.end())
			return mData.find(address)->second;
		else 
			return 0;
 	}
};


class Environment {
   std::vector<StackFrame> mStack;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;
   CallExpr * mCall;
   int returnVal;
   Heap mHeap;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL), mHeap() {
   }

   /// Initialize the Environment
   void init(TranslationUnitDecl * unit) {
	   mStack.push_back(StackFrame());	//	global varible
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }else if (VarDecl *vardecl = dyn_cast<VarDecl>(*i)) {
				visitVarDecl(vardecl);
         }
	   }
	   // mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   void binop(BinaryOperator *bop) {
   	// cout<< "  binop" << endl;
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();
	   // get operator code
	   int opCode = bop->getOpcode();

	   // =
	   if (bop->isAssignmentOp()) {
	   	// cout<< "  binop = " << bop << endl;
	   	// cout << "left " << left << " " << "left type " << (left->getType().getAsString()) << endl; 
	   	// cout << "right " << right << " " << "right type " << (right->getType().getAsString()) << endl; 
	   	int val = mStack.back().getStmtVal(right);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
   			// cout << "binop declrefexpr " << endl;
   			Decl * decl = declexpr->getFoundDecl();
   			val = mStack.back().getStmtVal(right);
		   	// cout << "binop declrefexpr "<< (declexpr->getFoundDecl()->getNameAsString()) << " " << decl << "  " << val << endl;
		   	mStack.back().bindDecl(decl, val);
		   } 
		   if(UnaryOperator * unaryop = dyn_cast<UnaryOperator>(left)){
				// cout << "binop_UnaryOperator " << endl;
				int addr = mStack.back().getStmtVal(unaryop->getSubExpr());

				val = mStack.back().getStmtVal(right);
				mHeap.Update(addr, val);
        	}
        	if(auto array = dyn_cast<ArraySubscriptExpr>(left)) {
        		// cout << "binop array " << endl;
        		int addr = mStack.back().getStmtVal(array);
        		val = mStack.back().getStmtVal(right);
        		mHeap.Update(addr, val);
        		// cout << "addr & val "  << addr << " " << val << endl;
        	}
    		mStack.back().bindStmt(bop, val);
		   
	   } else if (bop->isAdditiveOp() || bop->isMultiplicativeOp() || bop->isComparisonOp()) {
		   // left&right value
		   int valLeft = mStack.back().getStmtVal(left);
		   int valRight = mStack.back().getStmtVal(right);
		   // left + right
		   if(opCode == BO_Add) {
		   	// cout<< "  binop + "  << "val " << (valLeft + valRight) << endl;
		   	mStack.back().bindStmt(bop, valLeft + valRight);

		   }
		   // left - right
		   if(opCode == BO_Sub) {
		   	// cout<< "  binop - " << endl;
		   	mStack.back().bindStmt(bop, valLeft - valRight);
		   }
		   // left * right
		   if(opCode == BO_Mul) {
		   	// cout<< "  binop * " << endl;
		   	mStack.back().bindStmt(bop, valLeft * valRight);
		   }
		   if(opCode == BO_Div) {
		   	// cout << " bindnop / " << endl;
		   	mStack.back().bindStmt(bop, valLeft / valRight);
		   }
		   // left < right
		   if(opCode == BO_LT) {
		   	// cout<< "  binop < " << endl;
		   	if(valLeft < valRight) 
		   		mStack.back().bindStmt(bop, 1);
		   	else 
	   			mStack.back().bindStmt(bop, 0);
		   }
		   // left > right
		   if(opCode == BO_GT) {
		   	// cout<< "  binop > " << endl;
		   	if(valLeft > valRight) 
		   		mStack.back().bindStmt(bop, 1);
		   	else 
	   			mStack.back().bindStmt(bop, 0);
		   }
		   // ==
		   if(opCode == BO_EQ) {
		   	if(valLeft == valRight) 
		   		mStack.back().bindStmt(bop, 1);
		   	else
	   			mStack.back().bindStmt(bop, 0);
		   }
	   }
   }
   // decl
   void decl(DeclStmt * declstmt) {

	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {	// if decl is vardecl
			   // cout << "declstmt is vardecl " << (vardecl->getDefinition()->getNameAsString()) << " " << vardecl << endl;
			   if(vardecl->hasInit()) {	
		   		Expr * expr = vardecl->getInit();			// vardecl to expr
	   			
		   		if (IntegerLiteral * integer = dyn_cast<IntegerLiteral>(expr)){
		   			// cout<< "init decl IntegerLiteral " << endl;
		   			int val = integer->getValue().getSExtValue();
		   			mStack.back().bindDecl(vardecl, val);		// binder(refers next statement)			
		   		} 
			   } else if(vardecl->getType()->isArrayType())  {
			   	// cout << "isArrayType" << endl;
	            // Get array size
	            auto array = dyn_cast<ConstantArrayType>(vardecl->getType());
	            int array_size = array->getSize().getSExtValue();
      			// cout << "array size " << array_size << endl;
	            //NOTE that array is placed in heap
	            int address = mHeap.Malloc(array_size);
	            mStack.back().bindDecl(vardecl, address);
	          
			   } else if(vardecl->getType()->isPointerType()) {
			   	// cout << "vardecl is pointerType" << endl;
			   	mStack.back().bindDecl(vardecl, 0);
			   } else {
			   	// cout<< " decl not init" << endl;
			   	mStack.back().bindDecl(vardecl, 0);
			   }
		   }
	   }
   }
   // declref
   void declref(DeclRefExpr * declref) {
	 	QualType qualType = declref->getType();
	   mStack.back().setPC(declref);
	   Decl* decl = declref->getFoundDecl();
	   
	   if (qualType->isPointerType() || qualType->isArrayType()) {
	   	// cout << " isPointerType || isArrayType" << endl;
			int val = mStack.back().getDeclVal(decl);
			// cout << "decl " << decl <<  " val " << val << endl;
        	mStack.back().bindStmt(declref, val);
    	} else if (declref->getType()->isIntegerType()) {
	   	// cout<< "integer  declref " << (declref->getFoundDecl()->getNameAsString()) << " " << declref << endl;
		   int val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
	   } 
   }
	// cast
	void cast(CastExpr * castexpr) {
   	mStack.back().setPC(castexpr);
	   if (castexpr->getType()->isIntegerType()) {
		   Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmt(castexpr, val );
		   // cout<< "integer  cast " << castexpr << " val " << val << endl;
	   } else {
	   	string type = castexpr->getType().getAsString();
	   	// cout<< " other cast " << type << endl;
	   }
   }

   // !TODO Support Function Call
   FunctionDecl * call(CallExpr * callexpr) {
   	// cout<< "  call" << endl;
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);
		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
   		// cout<< "  PRINT" << endl;
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val << "\n";
	   } else if (callee == mMalloc) {
	   	// cout << "mMalloc"  << endl;
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
	   	int addr = mHeap.Malloc(val);
   		mStack.back().bindStmt(callexpr, addr);
   		// cout << "mMalloc addr " << callexpr << " " << callee << endl;

	   } else if (callee == mFree) {
 			// cout << "call mFree"  << endl;
 			Expr * decl = callexpr->getArg(0);
 			// decl->getName()
	   	// cout << "free decl " << decl << endl;
 			val = mStack.back().getStmtVal(decl);
 			// cout << " free  val "   << val << endl;	
 			mHeap.Free(val);
	   } else {
			// You could add your code here for Function call Return
	     	// cout<< "other call" << endl;
	     	for (int i = 0, n = callexpr->getNumArgs(); i < n; ++i) {     
         	Expr * expr = callexpr->getArg(i);
	         val = mStack.back().getStmtVal(expr); 
	         Decl * decl = dyn_cast<Decl>(callee->getParamDecl(i));
	         mStack.back().bindDecl(decl, val);
	     	}
	     	return callee;
	   }
     	return nullptr;
   }

	// integer
	void integerLiteral(IntegerLiteral * integer){
		int val = integer->getValue().getSExtValue();
		mStack.back().bindStmt(integer, val);
	}

	void implicitCastExpr(ImplicitCastExpr* implicitCastExpr) {

 		CastKind castKind = implicitCastExpr->getCastKind();
    	Expr *castedExpr = implicitCastExpr->getSubExpr();

    	if (castKind == CK_LValueToRValue) {
        int value ;

        auto unaryOperator = dyn_cast<UnaryOperator>(castedExpr);
        auto declRef = dyn_cast<DeclRefExpr>(castedExpr);
        auto arrayExpr = dyn_cast<ArraySubscriptExpr>(castedExpr);

        if (declRef) {
				// cout << "implicitcast_DeclRefExpr" << endl;
            value = mStack.back().getStmtVal(castedExpr);
            mStack.back().bindStmt(implicitCastExpr, value);
        	} else if (unaryOperator) {
        		// cout << "unary_operator" << endl;
            if (unaryOperator->getOpcode() == UO_Deref) {
            	Expr * expr = unaryOperator->getSubExpr();
            	// cout << "implicitcast *" << endl;
            	value = mStack.back().getStmtVal(expr);
            	int val = mHeap.get(value);
              	mStack.back().bindStmt(implicitCastExpr, val);                
            }
        	} else if (arrayExpr) {
            // auto member_size = getArrayMemberSize(array);
          	value = mStack.back().getStmtVal(castedExpr);
          	// cout << "CK_LValueToRValue array " << castedExpr << " val " << value << endl;
          	int val = mHeap.get(value);
          	// cout << "array val " << val << endl;
           	mStack.back().bindStmt(implicitCastExpr, val);   
        }
     	}
     	else if (castKind == CK_IntegralCast || castKind == CK_ArrayToPointerDecay || castKind == CK_BitCast){
        // cout << "implicitcast integer || ArraySubscriptExpr" << endl;
        int value = mStack.back().getStmtVal(castedExpr);
        mStack.back().bindStmt(implicitCastExpr, value);
    	} 
	}

	void visitVarDecl(VarDecl * vardecl) {
		// cout << "visitVarDecl" << endl;
		Expr * expr = vardecl->getInit();
		if(expr) {
			if (IntegerLiteral * intergerLiteral = dyn_cast<IntegerLiteral>(expr)) {
				int val = intergerLiteral->getValue().getSExtValue();
				mStack.back().bindDecl(vardecl, val);
			}
		} else {
			mStack.back().bindDecl(vardecl, 0);
		}
	}

	void returnStmt(ReturnStmt *returnStmt) {
		// cout << "setReturnVal" << endl;
		returnVal = mStack.back().getStmtVal(returnStmt->getRetValue());
	}

   int getReturnVal() {
        // cout << "getReturnVal" << endl;
        return returnVal;
 	}   

	void bindCallExpr(CallExpr *callexpr, int returnVal) {	
		// cout << "bindCallExpr" << endl;
		mStack.back().bindStmt(callexpr, returnVal);
	}

	void unaryOperator(UnaryOperator * uop) {
		// cout << "unaop" << endl;
		int opCode = uop->getOpcode();
		Expr * expr = uop->getSubExpr();
		// * operator
		if(opCode == UO_Deref) {
			// cout << "* operator" << endl;
	      
       	// cout << "* expr " << expr << endl;
      	// cout << "operator name " << (uop->getOpcodeStr(opCode)) << endl;
		   int addr = mStack.back().getStmtVal(expr);
		   // cout << "* addr " << addr << endl;
			int val = mHeap.get(addr);
			// cout << "* val " << val << endl;
			mStack.back().bindStmt(uop, val);
		} else if(opCode == UO_Minus) {
			int val = mStack.front().getStmtVal(expr);
			val = -val;
			// cout <<  val << endl;
			mStack.front().bindStmt(uop, val);
		}
	}

	void unaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr * uettExpr) {
      // cout << "unaryExprOrTypeTraitExpr" << endl;
      if((uettExpr->getArgumentType().getTypePtr())->isIntegerType()||(uettExpr->getArgumentType().getTypePtr())->isPointerType()){
        mStack.back().bindStmt(uettExpr, 1);
     	}else{
       mStack.back().bindStmt(uettExpr, 0);
     	}
	}

 	// explicit type cast
 	void cStyleCastExpr(CStyleCastExpr * cStyleCastExpr) {
 		Expr * expr = cStyleCastExpr->getSubExpr();
 		int val = mStack.back().getStmtVal(expr);
 		mStack.back().bindStmt(cStyleCastExpr, val);
 	}

 	// parenthesized expression (),[]
 	void parenExpr(ParenExpr * parenExpr) {
        Expr * expr = parenExpr->getSubExpr();
        int val = mStack.back().getStmtVal(expr);
        mStack.back().bindStmt(parenExpr, val);
 	}


 	void arraySubscriptExpr(ArraySubscriptExpr * array){
		// llvm::errs()<<"**arrayExpr**"<<"\n";
		Expr * left = array->getBase();
		Expr * right = array->getIdx();
		// cout << "array base & index  " << left << " " << right << endl; 
		int addr = mStack.back().getStmtVal(left);
		int i = mStack.back().getStmtVal(right);
		// cout << "array addr & val  " << addr << " [?] " << i << endl; 
		mStack.back().bindStmt(array, addr+i);
   }

 	int getVal(Expr * expr) { 
 		int val = mStack.back().getStmtVal(expr);
 		return val;
 	}

};


