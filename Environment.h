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
   	cout<< " bindDecl " << decl << " val " << val << endl;
		mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {
   	cout<< "  getDeclVal " << decl << endl;
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(Stmt * stmt, int val) {
   	cout<< "  bindStmt " << stmt << " val " << val << endl;
	   mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {
   	cout<< "  getStmtVal " << stmt << endl;
	   assert (mExprs.find(stmt) != mExprs.end());
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
	map<int, void*>mBuf;
   map<int, int> mContents;	
	map<void*, int>mAddr;
public:
	int Malloc(int size) {
		int * addr = (int *)malloc(size * sizeof(int));
		int buf = (unsigned long)addr;
		mAddr.insert(make_pair(addr, size));
		mBuf.insert(make_pair(buf, addr));
		cout<< "addr " << addr << " addr_int " << buf << endl;
      // Initialize the Content
      for (int i=0; i < size; ++i) {
      	mContents.insert(std::make_pair(buf+i, 0));
      }
		return buf;
	}
	void Free(int address) {
		void* addr = mBuf.find(address)->second;
		int size = mAddr.find(addr)->second;
		cout << "free size " << size << endl;
		for(int i = 0; i < size; ++i) {

			mContents.erase( address + i );
		}
		mBuf.erase(address);
		free(addr);
		cout << "mContents " << (mContents.find(address)->second) << endl;
		// assert(mAddr.find(address) != mAddr.end());
		// free(address);
	}
	void Update(int address, int val) {
   	mContents[address] = val;
	}
	int get(int address) {
		if (mContents.find(address) != mContents.end())
			return mContents.find(address)->second;
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
   	cout<< "  init" << endl;
	   mStack.push_back(StackFrame());	//	global varible
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }else if (VarDecl *vardecl = dyn_cast<VarDecl>(*i)) {
				cout<< "init VarDecl  " << (vardecl->getDefinition()->getNameAsString()) << endl;
				visitVarDecl(vardecl);
         }
	   }
	   // mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   int binop(BinaryOperator *bop) {
   	cout<< "  binop" << endl;
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();
	   cout << "left & right type " << (left->getType().getAsString()) << " " << (right->getType().getAsString())  << endl;
	   // get operator code
	   int opCode = bop->getOpcode();

	   // =
	   if (bop->isAssignmentOp()) {
	   	cout<< "  binop = " << endl;
	   	int val = mStack.back().getStmtVal(right);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
   			Decl * decl = declexpr->getFoundDecl();

		   	cout << "binop declrefexpr "<< (declexpr->getFoundDecl()->getNameAsString()) << " " << decl << "  " << val << endl;
		   	mStack.back().bindDecl(decl, val);
		   } 
		   if(UnaryOperator * unaryop = dyn_cast<UnaryOperator>(left)){
				cout << "binop_UnaryOperator " << endl;
				int addr = mStack.back().getStmtVal(unaryop->getSubExpr());
				mHeap.Update(addr, val);
        	} 
    		mStack.back().bindStmt(bop, val);
		   
	   } else if (bop->isComparisonOp() || bop->isAdditiveOp()
               || bop->isMultiplicativeOp()) {
		   // left&right value
		   int valLeft = mStack.back().getStmtVal(left);
		   int valRight = mStack.back().getStmtVal(right);
		   // left + right
		   if(opCode == BO_Add) {
		   	cout<< "  binop + " << endl;
		   	mStack.back().bindStmt(bop, valLeft + valRight);
		   }
		   // left - right
		   if(opCode == BO_Sub) {
		   	cout<< "  binop - " << endl;
		   	mStack.back().bindStmt(bop, valLeft - valRight);
		   }
		   // left * right
		   if(opCode == BO_Mul) {
		   	mStack.back().bindStmt(bop, valLeft * valRight);
		   }
		   // left < right
		   if(opCode == BO_LT) {
		   	cout<< "  binop < " << endl;
		   	if(valLeft < valRight) {
		   		return 1;
		   	}
		   	else {
		   		return -1;
		   	}
		   }
		   // left > right
		   if(opCode == BO_GT) {
		   	if(valLeft > valRight) {
		   		return 1;
		   	}
		   	else {
		   		return -1;
		   	}
		   }
	   }

	   return 0;
   }
   // decl
   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {	// if decl is vardecl
			   cout << "declstmt is vardecl " << (vardecl->getDefinition()->getNameAsString()) << endl;
			   if(vardecl->hasInit()) {	
		   		Expr * expr = vardecl->getInit();			// vardecl to expr
	   			
		   		if(UnaryOperator * unaryop = dyn_cast<UnaryOperator>(expr)) {
             		cout << "init decl UnaryOperator" << endl;
             		Expr * subExpr = unaryop->getSubExpr();
             		QualType qualType = subExpr->getType();
             		cout << "unaryop's subExpr is UnaryOperator? " << (qualType->isPointerType()) << endl;


             		int val = mStack.back().getStmtVal(subExpr);
              		int addr = mHeap.Malloc(1);
						mHeap.Update(addr, val);
              		mStack.back().bindDecl(vardecl, addr);
		   		} else if (IntegerLiteral * integer = dyn_cast<IntegerLiteral>(expr)){
		   			cout<< "init decl IntegerLiteral " << endl;
		   			int val = integer->getValue().getSExtValue();
		   			mStack.back().bindDecl(vardecl, val);		// binder(refers next statement)			
		   		} 
			   }
			   else {
			   	cout<< " decl not init" << endl;
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
	   	cout << " isPointerType || isArrayType" << endl;
			int val = mStack.back().getDeclVal(decl);
        	mStack.back().bindStmt(declref, val);
    	} else if (declref->getType()->isIntegerType()) {
	   	cout<< "integer  declref " << (declref->getFoundDecl()->getNameAsString()) << " " << declref << endl;
		   
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
		   cout<< "integer  cast " << castexpr << " val " << val << endl;
	   } else {
	   	string type = castexpr->getType().getAsString();
	   	cout<< " other cast " << type << endl;
	   }
   }

   // !TODO Support Function Call
   FunctionDecl * call(CallExpr * callexpr) {
   	cout<< "  call" << endl;
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);
		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
   		cout<< "  PRINT" << endl;
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val << "\n";
	   } else if (callee == mMalloc) {
	   	cout << "mMalloc"  << endl;
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
	   	int addr = mHeap.Malloc(val);
   		mStack.back().bindStmt(decl, addr);

	   } else if (callee == mFree) {
 			Expr * decl = callexpr->getArg(0);
	   	cout << "free decl " << decl ;
 			val = mStack.back().getStmtVal(decl);
 			cout << " free  val "   << val << endl;
 			
 			mHeap.Free(val);
	   } else {
			// You could add your code here for Function call Return
	     	cout<< "other call" << endl;
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
	void integer(IntegerLiteral * integer){
		int val = integer->getValue().getSExtValue();
		mStack.back().bindStmt(integer, val);
	}

	void implicitcast(ImplicitCastExpr* implicitCastExpr) {

 		CastKind castKind = implicitCastExpr->getCastKind();
    	Expr *castedExpr = implicitCastExpr->getSubExpr();

    	if (castKind == CK_LValueToRValue) {
        int value ;

        auto unary_operator = dyn_cast<UnaryOperator>(castedExpr);
        auto decl_ref = dyn_cast<DeclRefExpr>(castedExpr);
        auto array = dyn_cast<ArraySubscriptExpr>(castedExpr);

        if (decl_ref) {
				cout << "implicitcast_DeclRefExpr" << endl;
            value = mStack.back().getStmtVal(castedExpr);
            mStack.back().bindStmt(implicitCastExpr, value);
        	} else if (unary_operator) {
            UnaryOperator * uop = dyn_cast<UnaryOperator>(unary_operator);
            if (uop->getOpcode() == UO_Deref) {
            	Expr * expr = uop->getSubExpr();
            	cout << "implicitcast *" << endl;
            	value = mStack.back().getStmtVal(expr);
              	mStack.back().bindStmt(implicitCastExpr, value);                
            }

        } 
     	}
     	else if(castKind == CK_FunctionToPointerDecay) {
     		cout << "implicitcast_CK_FunctionToPointerDecay" << endl;
     		return;
     	}
     	else if (castKind == CK_IntegralCast || castKind == CK_ArrayToPointerDecay){
        cout << "implicitcast integer || ArraySubscriptExpr" << endl;
        int value = mStack.back().getStmtVal(castedExpr);
        mStack.back().bindStmt(implicitCastExpr, value);

    } 

	}

	void visitVarDecl(VarDecl * vardecl) {
		cout << "visitVarDecl" << endl;
		Expr * expr = vardecl->getInit();
		if (IntegerLiteral * intergerLiteral = dyn_cast<IntegerLiteral>(expr)) {
			int val = intergerLiteral->getValue().getSExtValue();
			mStack.back().bindDecl(vardecl, val);
		}
	}

	void setReturnVal(ReturnStmt *returnStmt) {
		cout << "setReturnVal" << endl;
		returnVal = mStack.back().getStmtVal(returnStmt->getRetValue());
	}

   int getReturnVal() {
        cout << "getReturnVal" << endl;
        return returnVal;
 	}   

	void bindCallExpr(CallExpr *callexpr, int returnVal) {	
		cout << "bindCallExpr" << endl;
		mStack.back().bindStmt(callexpr, returnVal);
	}

	void unaryOp(UnaryOperator * uop) {
		cout << "unaop" << endl;
		int opCode = uop->getOpcode();
		// * operator
		if(opCode == UO_Deref) {
			cout << "* operator" << endl;
	      Expr * expr = uop->getSubExpr();
       	cout << "* expr " << expr << endl;
      	// cout << "operator name " << (uop->getOpcodeStr(opCode)) << endl;
		   int addr = mStack.back().getStmtVal(expr);
		   cout << "* addr " << addr << endl;
			int val = mHeap.get(addr);
			cout << "* val " << val << endl;
			mStack.back().bindStmt(uop, val);
		}
	}

	void unaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr * uettExpr) {
      cout << "unaryExprOrTypeTraitExpr" << endl;
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


 	void arrayExpr(ArraySubscriptExpr * array){
		llvm::errs()<<"**arrayExpr**"<<"\n";
		Expr * left = array->getLHS();
		Expr * right = array->getRHS();
		int addr = mStack.back().getStmtVal(left);
		int i = mStack.back().getStmtVal(right);
		int val = mHeap.get(addr + i);
		mStack.back().bindStmt(array, val);
   }



};


