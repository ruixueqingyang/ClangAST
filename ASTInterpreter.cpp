//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace std;

#include "Environment.h"
#include <iostream>

class InterpreterVisitor : 
    public EvaluatedExprVisitor<InterpreterVisitor> {
public:
    explicit InterpreterVisitor(const ASTContext &context, Environment * env)
    : EvaluatedExprVisitor(context), mEnv(env) {}
    virtual ~InterpreterVisitor() {}
    // Binary Operator
    virtual void VisitBinaryOperator (BinaryOperator * bop) {
      cout << "VisitBinaryOperator" << endl;
      VisitStmt(bop);
      opFlag = mEnv->binop(bop);
    }
    // Declaration referance expression
    virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
      cout << "VisitDeclRefExpr" << endl;
      VisitStmt(expr);
      mEnv->declref(expr);
    }
    // Cast expression
    virtual void VisitCastExpr(CastExpr * expr) {
      cout << "VisitCastExpr" << endl;
      VisitStmt(expr);
      mEnv->cast(expr);
    }
    // Call expression
    virtual void VisitCallExpr(CallExpr * call) {
      cout << "VisitCallExpr" << endl;
      VisitStmt(call);
      if(FunctionDecl * funcdecl = mEnv->call(call)) {
        VisitStmt(funcdecl->getBody());
        mEnv->bindCallExpr(call,mEnv->getReturnVal());
      }
      
    }
    // Declaration statement
    virtual void VisitDeclStmt(DeclStmt * declstmt) {
      cout << "VisitDeclStmt" << endl;
      VisitStmt(declstmt);  //  int a = 0;
      mEnv->decl(declstmt);
    }
    // Integer
    virtual void VisitIntegerLiteral(IntegerLiteral * integer) {      
      cout << "VisitIntegerLiteral" << endl;
      // VisitStmt(integer);
      mEnv->integer(integer);
    }
    // While
    virtual void VisitWhileStmt(WhileStmt * whilestmt) {
      cout << "VisitWhileStmt" << endl;
      while(1) {
        VisitBinaryOperator((BinaryOperator *)whilestmt->getCond());
        if(opFlag == -1) { // binop return value
          break;
        }
        VisitStmt(whilestmt->getBody());
      }
    }
    // For
    virtual void VisitForStmt(ForStmt * forstmt) {
      cout << "VisitForStmt" << endl;
      while(1) {
        VisitBinaryOperator((BinaryOperator *)forstmt->getCond());  // for statement condition
        if(opFlag == -1) {
          break;
        }
        VisitBinaryOperator((BinaryOperator *)forstmt->getInc());   // +1
        VisitStmt(forstmt->getBody());
      }
    }
    // ImplicitCastExpr
    virtual void VisitImplicitCastExpr(ImplicitCastExpr* imcastexpr) {
      VisitStmt(imcastexpr);
      cout<< "VisitImplicitCastExpr" << endl;
      mEnv->implicitcast(imcastexpr);
    }
    // return
    virtual void VisitReturnStmt(ReturnStmt *returnStmt) {
        cout<< "VisitReturnStmt" << endl;
        VisitStmt(returnStmt);
        mEnv->setReturnVal(returnStmt);
    }
    // unary operator
    virtual void VisitUnaryOperator(UnaryOperator* uop) {
      cout<< "VisitUnaryOperator" << endl;
      VisitStmt(uop);
      mEnv->unaryOp(uop);
    }
    virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr * unaryExprOrTypeTraitExpr) {
      cout<< "VisitUnaryExprOrTypeTraitExpr" << endl;
      mEnv->unaryExprOrTypeTraitExpr(unaryExprOrTypeTraitExpr);
    }
    virtual void VisitArraySubscriptExpr(ArraySubscriptExpr * array) {
      VisitStmt(array);
      mEnv->arrayExpr(array);
   }

   virtual void VisitParenExpr(ParenExpr * parenExpr) {
   	cout << "ParenExpr " << endl;
		VisitStmt(parenExpr);
		mEnv->parenExpr(parenExpr);
   } 

   virtual void VisitCStyleCastExpr(CStyleCastExpr * cStyleCastExpr) {
   	cout << "CStyleCastExpr " << endl;
   	VisitStmt(cStyleCastExpr);
   	mEnv->cStyleCastExpr(cStyleCastExpr);
   }
private:
    Environment * mEnv;
    int opFlag;
};
// ASTConsumer 
class InterpreterConsumer : public ASTConsumer {
public:
    explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
      mVisitor(context, &mEnv) {
    }
    virtual ~InterpreterConsumer() {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
      TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
      mEnv.init(decl);
      FunctionDecl * entry = mEnv.getEntry();
      mVisitor.VisitStmt(entry->getBody());
    }
private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public: 
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
      return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main (int argc, char ** argv) {
  if (argc > 1) {
    clang::tooling::runToolOnCode(new InterpreterClassAction, argv[1], "xxx.c");
   }
}
