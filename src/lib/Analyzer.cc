//===-- Analyzer.cc - the kernel-analysis framework-------------===//
//
// It constructs a global call-graph based on multi-layer type
// analysis.
//
//===-----------------------------------------------------------===//

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Path.h"

#include <memory>
#include <vector>
#include <sstream>
#include <sys/resource.h>
#include <iomanip>

#include "Analyzer.h"
#include "CallGraph.h"
#include "Config.h"

using namespace llvm;

cl::opt<std::string> OutputFile(
	"output",
	cl::desc("Specify the output file"),
	cl::init(""));

// Command line parameters.
cl::list<std::string> InputFilenames(
	cl::Positional, cl::ZeroOrMore, cl::desc("<input bitcode files>"));

cl::opt<std::string> SrcRoot(
    "src-root",
    cl::desc("Specify the root directory of the source files"),
    cl::Required);

cl::opt<std::string> BCListFile(
	"bc-list", cl::desc("Specify the file that contains the list of bitcode files"),
	cl::init(""));

cl::opt<unsigned> VerboseLevel(
	"verbose-level", cl::desc("Print information at which verbose level"),
	cl::init(0));

cl::opt<int> MLTA(
	"mlta",
	cl::desc("Multi-layer type analysis for refining indirect-call \
		targets"),
	cl::NotHidden, cl::init(0));

cl::opt<int> TyPM(
	"typm",
	cl::desc("Type-based dependence analysis for program modularization \
		targets"),
	cl::NotHidden, cl::init(1));
GlobalContext GlobalCtx;

cl::opt<int> PHASE(
	"phase",
	cl::desc("How many iterations? \
		targets"),
	cl::NotHidden, cl::init(2));

void IterativeModulePass::run(ModuleList &modules)
{

	ModuleList::iterator i, e;
	OP << "[" << ID << "] Initializing " << modules.size() << " modules\n";
	bool again = true;
	while (again)
	{
		again = false;
		for (i = modules.begin(), e = modules.end(); i != e; ++i)
		{
			again |= doInitialization(i->first);
			OP << ".";
		}
	}
	OP << "\n";

	unsigned iter = 0, changed = 1;
	while (changed)
	{
		++iter;
		changed = 0;
		unsigned counter_modules = 0;
		unsigned total_modules = modules.size();
		for (i = modules.begin(), e = modules.end(); i != e; ++i)
		{
			OP << "[" << ID << " / " << iter << "] ";
			OP << "[" << ++counter_modules << " / " << total_modules << "] ";
			OP << "[" << i->second << "]\n";

			bool ret = doModulePass(i->first);
			if (ret)
			{
				++changed;
				OP << "\t [CHANGED]\n";
			}
			else
				OP << "\n";
		}
		OP << "[" << ID << "] Updated in " << changed << " modules.\n";
	}

	OP << "[" << ID << "] Postprocessing ...\n";
	again = true;
	while (again)
	{
		again = false;
		for (i = modules.begin(), e = modules.end(); i != e; ++i)
		{
			// TODO: Dump the results.
			again |= doFinalization(i->first);
		}
	}

	OP << "[" << ID << "] Done!\n\n";
}

void PrintResults(GlobalContext *GCtx)
{

	int TotalTargets = 0;
	for (auto IC : GCtx->IndirectCallInsts)
	{
		TotalTargets += GCtx->Callees[IC].size();
	}
	float AveIndirectTargets = 0.0;
	if (GCtx->NumValidIndirectCalls)
		AveIndirectTargets =
			(float)GCtx->NumIndirectCallTargets / GCtx->IndirectCallInsts.size();

	int totalsize = 0;
	for (auto &curEle : GCtx->Callees)
	{
		if (curEle.first->isIndirectCall())
		{
			totalsize += curEle.second.size();
		}
	}
	OP << "\n@@ Total number of final callees: " << totalsize << "\n";

	OP << "############## Result Statistics ##############\n";
	cout << "# Ave. Number of indirect-call targets: \t" << std::setprecision(5) << AveIndirectTargets << "\n";
	OP << "# Number of indirect calls: \t\t\t" << GCtx->IndirectCallInsts.size() << "\n";
	OP << "# Number of indirect calls with targets: \t" << GCtx->NumValidIndirectCalls << "\n";
	OP << "# Number of indirect-call targets: \t\t" << GCtx->NumIndirectCallTargets << "\n";
	OP << "# Number of address-taken functions: \t\t" << GCtx->AddressTakenFuncs.size() << "\n";
	OP << "# Number of second layer calls: \t\t" << GCtx->NumSecondLayerTypeCalls << "\n";
	OP << "# Number of second layer targets: \t\t" << GCtx->NumSecondLayerTargets << "\n";
	OP << "# Number of first layer calls: \t\t\t" << GCtx->NumFirstLayerTypeCalls << "\n";
	OP << "# Number of first layer targets: \t\t" << GCtx->NumFirstLayerTargets << "\n";
}

int main(int argc, char **argv)
{

	// Print a stack trace if we signal out.
	sys::PrintStackTraceOnErrorSignal(argv[0]);
	PrettyStackTraceProgram X(argc, argv);

	llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

	cl::ParseCommandLineOptions(argc, argv, "global analysis\n");
	SMDiagnostic Err;

	SRC_ROOT = SrcRoot;

	if (!OutputFile.empty())
	{
		OUTPUT_FILE = std::make_unique<std::ofstream>(OutputFile, std::ios::out);
		if (!OUTPUT_FILE->is_open()) {
			errs() << "Error: Unable to open output file " << OutputFile << "\n";
			return 1;
		}
	}

	if (!BCListFile.empty())
	{
		std::ifstream file(BCListFile);
		std::string line;
		while (std::getline(file, line))
		{
			InputFilenames.push_back(line);
		}
	}

	// Loading modules
	OP << "Total " << InputFilenames.size() << " file(s)\n";

	for (unsigned i = 0; i < InputFilenames.size(); ++i)
	{

		LLVMContext *LLVMCtx = new LLVMContext();
		std::unique_ptr<Module> M = parseIRFile(InputFilenames[i], Err, *LLVMCtx);

		if (M == NULL)
		{
			OP << argv[0] << ": error loading file '"
			   << InputFilenames[i] << "'\n";
			continue;
		}

		Module *Module = M.release();
		StringRef MName = StringRef(strdup(InputFilenames[i].data()));
		GlobalCtx.Modules.push_back(std::make_pair(Module, MName));
		GlobalCtx.ModuleMaps[Module] = InputFilenames[i];
	}
	//
	// Main workflow
	//

	// Build global callgraph.

	ENABLE_MLTA = MLTA;
	ENABLE_TYDM = TyPM;
	MAX_PHASE_CG = PHASE;
	if (!ENABLE_TYDM)
		MAX_PHASE_CG = 1;

	CallGraphPass CGPass(&GlobalCtx);
	CGPass.run(GlobalCtx.Modules);
	// CGPass.processResults();

	// Print final results
	PrintResults(&GlobalCtx);

	return 0;
}
