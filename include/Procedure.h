#pragma once
#include "BasicBlock.h"
#include "locident.h"
#include "state.h"
#include "icode.h"
#include "StackFrame.h"
#include "CallConvention.h"

#include <llvm/ADT/ilist.h>
#include <memory>
#include <stdint.h>
#include <QtCore/QString>
#include <bitset>
#include <map>

class QIODevice;
class QTextStream;

/* PROCEDURE NODE */
struct CALL_GRAPH;
struct Expr;
struct Disassembler;
struct Function;
struct CALL_GRAPH;
struct PROG;
struct IStructuredTextTarget;
struct Function;

namespace llvm
{
// Traits for intrusive list of basic blocks...
template<>
struct ilist_traits<BB> : public ilist_default_traits<BB>
{

    // createSentinel is used to get hold of the node that marks the end of the
    // list... (same trick used here as in ilist_traits<Instruction>)
    BB *createSentinel() const {
        return static_cast<BB*>(&Sentinel);
    }
    static void destroySentinel(BB*) {}

    BB *provideInitialHead() const { return createSentinel(); }
    BB *ensureHead(BB*) const { return createSentinel(); }
    static void noteHead(BB*, BB*) {}

    //static ValueSymbolTable *getSymTab(Function *ItemParent);
private:
    mutable ilist_half_node<BB> Sentinel;
};
}
/* Procedure FLAGS */
enum PROC_FLAGS
{
    PROC_BADINST=0x00000100,    /* Proc contains invalid or 386 instruction */
    PROC_IJMP   =0x00000200,    /* Proc incomplete due to indirect jmp	 	*/
    PROC_ICALL  =0x00000400,    /* Proc incomplete due to indirect call	    */
    PROC_HLL    =0x00001000,    /* Proc is likely to be from a HLL	    	*/
    PROC_NEAR   =0x00010000,    /* Proc exits with near return	    	    */
    PROC_FAR    =0x00020000,    /* Proc exits with far return	    	    */
    GRAPH_IRRED =0x00100000,    /* Proc generates an irreducible graph	    */
    SI_REGVAR   =0x00200000,    /* SI is used as a stack variable 	    	*/
    DI_REGVAR   =0x00400000,    /* DI is used as a stack variable 	    	*/
    REG_ARGS    =0x01000000,    /* Proc has registers as arguments	    	*/
//    PROC_VARARG =0x02000000,	/* Proc has variable arguments	    	    */
    PROC_OUTPUT =0x04000000,    /* C for this proc has been output 	    	*/
    PROC_RUNTIME=0x08000000,    /* Proc is part of the runtime support	    */
    PROC_ISLIB  =0x10000000,    /* Proc is a library function	    	    */
    PROC_ASM    =0x20000000,    /* Proc is an intrinsic assembler routine   */
    PROC_IS_HLL =0x40000000     /* Proc has HLL prolog code	    	    	*/
//#define CALL_MASK    0xFFFF9FFF /* Masks off CALL_C and CALL_PASCAL	     	*/
};

struct Type {
    hlType dcc_type;
};
struct FunctionType : public Type
{
    CConv *         m_call_conv;
    std::vector<Type> ContainedTys;
    ID          retVal;    /* Return value - identifier    	     */
    bool m_vararg=false;
    unsigned 	getNumParams() const { return ContainedTys.size(); }
    bool isVarArg() const {return m_vararg;}
    void setReturnType(hlType t) {
        retVal.type = t;
    }
    void setReturnLocation(const LONGID_TYPE &v) {
        retVal.loc = REG_FRAME;
        retVal.longId() = v;
    }
    void setReturnLocation(eReg reg) {
        retVal.loc = REG_FRAME;
        retVal.id.regi = reg;
    }
    hlType getReturnType() const { return retVal.type; }
    void addArgument(hlType hl) {
        ContainedTys.push_back(Type {hl});
    }
    void clearArguments() { ContainedTys.clear(); }

    void setCallingConvention(CConv::CC_Type cc);

    static FunctionType *get(Type result,std::vector<Type> params, bool vararg_func) {
        FunctionType * res = new FunctionType;
        res->setReturnType(result.dcc_type);
        std::swap(res->ContainedTys,params);
        res->m_vararg = vararg_func;
        return res;
    }
};
struct Assignment
{
    Expr *lhs;
    Expr *rhs;
};
struct JumpTable
{
    uint32_t start;
    uint32_t finish;
    bool valid() {return start<finish;}
    size_t size() { return (finish-start)/2;}
    size_t entrySize() { return 2;}
    void pruneEntries(uint16_t cs);
};
class FunctionCfg
{
    std::list<BB*> m_listBB;      /* Ptr. to BB list/CFG                  	 */
public:
    typedef std::list<BB*>::iterator iterator;
    iterator	begin() {
        return m_listBB.begin();
    }
    iterator	end()	 {
        return m_listBB.end();
    }
    BB * &front() { return m_listBB.front();}
    void nodeSplitting()
    {
        /* Converts the irreducible graph G into an equivalent reducible one, by
         * means of node splitting.  */
        fprintf(stderr,"Attempt to perform node splitting: NOT IMPLEMENTED\n");
    }
    void push_back(BB *v) { m_listBB.push_back(v);}
};
typedef std::shared_ptr<struct Function> PtrFunction;
enum DecompilationStep : uint32_t {
    eNotDecoded,    // no processing done yet
    eDisassemblyInProgress,
    eDissassembled, // low level disassembly done
    //eLocatedImpureRefs,
    //eStackTracing, // tracing stack depth across function calls

};
class Function : public std::enable_shared_from_this<Function>
{
    typedef llvm::iplist<BB> BasicBlockListType;
    // BasicBlock iterators...
    typedef BasicBlockListType::iterator iterator;
    typedef BasicBlockListType::const_iterator const_iterator;
protected:
    BasicBlockListType  BasicBlocks;        ///< The basic blocks
    Function(FunctionType *ty) : nStep(eNotDecoded),procEntry(0),depth(0),flg(0),cbParam(0),m_dfsLast(0),numBBs(0),
        hasCase(false),liveAnal(0)
    {
        type = ty;
        if(!ty) // No type was provided, create it
            type = new FunctionType;
        callingConv(CConv::UNKNOWN);
    }

public:
    DecompilationStep nStep;      // decompilation step number for this function
    FunctionType *  type;
    uint32_t        procEntry; /* label number                         	 */
    QString         name;      /* Meaningful name for this proc     	 */
    STATE           state;     /* Entry state                          	 */
    int             depth;     /* Depth at which we found it - for printing */
    uint32_t        flg;       /* Combination of Icode & Proc flags    	 */
    int16_t         cbParam;   /* Probable no. of bytes of parameters  	 */
    STKFRAME        args;      /* Array of arguments                   	 */
    LOCAL_ID        localId;   /* Local identifiers                         */

        /* Icodes and control flow graph */
    CIcodeRec	 Icode;     /* Object with ICODE records                 */
    FunctionCfg     m_actual_cfg;
    std::vector<BB*> m_dfsLast;
    std::map<int,BB*> m_ip_to_bb;
//                           * (reverse postorder) order            	 */
    size_t        numBBs;    /* Number of BBs in the graph cfg       	 */
    bool         hasCase;   /* Procedure has a case node            	 */

    /* For interprocedural live analysis */
    LivenessSet     liveIn;	/* Registers used before defined                 */
    LivenessSet     liveOut;	/* Registers that may be used in successors	 */
    bool            liveAnal;	/* Procedure has been analysed already	     */

    virtual ~Function() {
        delete type;
    }
public:
    static PtrFunction Create(FunctionType *ty=0,int /*Linkage*/=0,const QString &nm="",void */*module*/=0)
    {
        PtrFunction r(new Function(ty));
        r->name = nm;
        return r;
    }
    hlType getReturnType() const {
        return getFunctionType()->getReturnType();
    }
    FunctionType *getFunctionType() const {
        return type;
    }
    CConv *callingConv() const { return type->m_call_conv;}
    void callingConv(CConv::CC_Type v);

//    bool anyFlagsSet(uint32_t t) { return (flg&t)!=0;}
    bool hasRegArgs() const { return (flg & REG_ARGS)!=0;}
    void markDoNotDecompile() { flg |= PROC_ISLIB; }
    bool doNotDecompile() const { return isLibrary(); }
    bool isLibrary() const { return (flg & PROC_ISLIB)!=0;}
    void compoundCond();
    void writeProcComments();
    void lowLevelAnalysis();
    void bindIcodeOff();
    void dataFlow(LivenessSet &liveOut);
    void compressCFG();
    void highLevelGen();
    void structure(derSeq *derivedG);
    derSeq *checkReducibility();
    void createCFG();
    void markImpure();
    void findImmedDom();
    void process_operands(ICODE &pIcode, STATE *pstate);
    bool process_JMP(ICODE &pIcode, STATE *pstate, CALL_GRAPH *pcallGraph);
    bool process_CALL(ICODE &pIcode, CALL_GRAPH *pcallGraph, STATE *pstate);
    void freeCFG();
    void codeGen(QIODevice & fs);
    void mergeFallThrough(BB *pBB);
    void structIfs();
    void structLoops(derSeq *derivedG);
    void buildCFG(Disassembler &ds);
    void controlFlowAnalysis();
    void newRegArg(iICODE picode, iICODE ticode);
    void writeProcComments(QTextStream & ostr);
    void toStructuredText(IStructuredTextTarget *out,int level);

    void displayCFG();
    void displayStats();
    void processHliCall(Expr *exp, iICODE picode);

    void preprocessReturnDU(LivenessSet &_liveOut);
    Expr * adjustActArgType(Expr *_exp, hlType forType);
    QString writeCall(Function *tproc, STKFRAME &args, int *numLoc);
    void processDosInt(STATE *pstate, PROG &prog, bool done);
    ICODE *translate_DIV(LLInst *ll, ICODE &_Icode);
    ICODE *translate_XCHG(LLInst *ll, ICODE &_Icode);

    void switchState(DecompilationStep s);
protected:
    void extractJumpTableRange(ICODE& pIcode, STATE *pstate, JumpTable &table);
    bool followAllTableEntries(JumpTable &table, uint32_t cs, ICODE &pIcode, CALL_GRAPH *pcallGraph, STATE *pstate);
    bool removeInEdge_Flag_and_ProcessLatch(BB *pbb, BB *a, BB *b);
    bool Case_X_and_Y(BB* pbb, BB* thenBB, BB* elseBB);
    bool Case_X_or_Y(BB* pbb, BB* thenBB, BB* elseBB);
    bool Case_notX_or_Y(BB* pbb, BB* thenBB, BB* elseBB);
    bool Case_notX_and_Y(BB* pbb, BB* thenBB, BB* elseBB);
    void replaceInEdge(BB* where, BB* which, BB* with);
    void processExpPush(int &numHlIcodes, iICODE picode);

    // TODO: replace those with friend visitor ?
    void propLongReg(int loc_ident_idx, const ID &pLocId);
    void propLongStk(int i, const ID &pLocId);
    void propLongGlb(int i, const ID &pLocId);
    void processTargetIcode(iICODE picode, int &numHlIcodes, iICODE ticode, bool isLong);

    int     findBackwarLongDefs(int loc_ident_idx, const ID &pLocId, iICODE iter);
    int     findForwardLongUses(int loc_ident_idx, const ID &pLocId, iICODE beg);
    void    structCases();
    void    findExps();
    void    genDU1();
    void    elimCondCodes();
    void    liveRegAnalysis(LivenessSet &in_liveOut);
    void    findIdioms();
    void    propLong();
    void    genLiveKtes();
    bool    findDerivedSeq(derSeq &derivedGi);
    bool    nextOrderGraph(derSeq &derivedGi);
    void    addOutEdgesForConditionalJump(BB*        pBB, int next_ip, LLInst *ll);

private:
    bool    decodeIndirectJMP(ICODE &pIcode, STATE *pstate, CALL_GRAPH *pcallGraph);
    bool    decodeIndirectJMP2(ICODE &pIcode, STATE *pstate, CALL_GRAPH *pcallGraph);
};

typedef std::list<PtrFunction> FunctionListType;
typedef FunctionListType lFunction;
typedef lFunction::iterator ilFunction;
