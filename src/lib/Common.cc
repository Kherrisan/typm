#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <fstream>
#include <regex>
#include "Common.h"
#include "Config.h"
#include <llvm/Support/Path.h>

// Map from struct elements to its name
static map<string, set<StringRef>> elementsStructNameMap;

bool trimPathSlash(string &path, int slash)
{
	while (slash > 0)
	{
		path = path.substr(path.find('/') + 1);
		--slash;
	}

	return true;
}

string getFileName(string SrcRoot, DILocation *Loc, DISubprogram *SP)
{
	string FN;
	if (Loc)
		FN = Loc->getFilename().str();
	else if (SP)
		FN = SP->getFilename().str();
	else
		return "";

	// Check if FN is already an absolute path
	if (!llvm::sys::path::is_absolute(FN)) {
		// If not absolute, prepend the appropriate source path
		// Remove leading slash from FN if present
		if (!FN.empty() && FN[0] == '/') {
			FN = FN.substr(1);
		}
		
		// Remove trailing slash from SrcRoot if present
		if (!SrcRoot.empty() && SrcRoot.back() == '/') {
			SrcRoot.pop_back();
		}
		
		FN = SrcRoot + "/" + FN;
	}

	return FN;
}

/// Check if the value is a constant.
bool isConstant(Value *V)
{
	// Invalid input.
	if (!V)
		return false;

	// The value is a constant.
	Constant *Ct = dyn_cast<Constant>(V);
	if (Ct)
		return true;

	return false;
}

/// Get the source code line
string getSourceLine(string fn_str, unsigned lineno)
{
	std::ifstream sourcefile(fn_str);
	string line;
	sourcefile.seekg(ios::beg);

	for (int n = 0; n < lineno - 1; ++n)
	{
		sourcefile.ignore(std::numeric_limits<streamsize>::max(), '\n');
	}
	getline(sourcefile, line);

	return line;
}

string getSourceFuncName(Instruction *I, string SrcRoot)
{

	DILocation *Loc = getSourceLocation(I);
	if (!Loc)
		return "";
	unsigned lineno = Loc->getLine();
	std::string fn_str = getFileName(SrcRoot, Loc);
	string line = getSourceLine(fn_str, lineno);

	while (line[0] == ' ' || line[0] == '\t')
		line.erase(line.begin());
	line = line.substr(0, line.find('('));
	return line;
}

string getValueName(Value *V)
{
	std::string Str;
	raw_string_ostream OS(Str);
	V->printAsOperand(OS, false);
	return OS.str();
}

string extractMacro(string line, Instruction *I)
{
	string macro, word, FnName;
	std::regex caps("[^\\(][_A-Z][_A-Z0-9]+[\\);,]+");
	smatch match;

	// detect function macros
	if (CallInst *CI = dyn_cast<CallInst>(I))
	{
		FnName = getCalledFuncName(CI).str();
		caps = "[_A-Z][_A-Z0-9]{2,}";
		std::regex keywords("(\\s*)(for|if|while)(\\s*)(\\()");

		if (regex_search(line, match, keywords))
			line = line.substr(match[0].length());

		if (line.find(FnName) != std::string::npos)
		{
			if (regex_search(FnName, match, caps))
				return FnName;
		}
		else
		{
			// identify non matching functions as macros
			// std::count(line.begin(), line.end(), '"') > 0
			std::size_t eq_pos = line.find_last_of("=");
			if (eq_pos == std::string::npos)
				eq_pos = 0;
			else
				++eq_pos;

			std::size_t paren = line.find('(', eq_pos);
			return line.substr(eq_pos, paren - eq_pos);
		}
	}
	else
	{
		// detect macro constant variables
		std::size_t lhs = -1;
		stringstream iss(line.substr(lhs + 1));

		while (iss >> word)
		{
			if (regex_search(word, match, caps))
			{
				macro = word;
				return macro;
			}
		}
	}

	return "";
}

/// Get called function name of V.
StringRef getCalledFuncName(CallInst *CI)
{

	Value *V;
	V = CI->getCalledOperand();
	assert(V);

	InlineAsm *IA = dyn_cast<InlineAsm>(V);
	if (IA)
		return StringRef(IA->getAsmString());

	User *UV = dyn_cast<User>(V);
	if (UV)
	{
		if (UV->getNumOperands() > 0)
		{
			Value *VUV = UV->getOperand(0);
			return VUV->getName();
		}
	}

	return V->getName();
}

DILocation *getSourceLocation(Instruction *I)
{
	if (!I)
		return NULL;

	MDNode *N = I->getMetadata("dbg");
	if (!N)
		return NULL;

	DILocation *Loc = dyn_cast<DILocation>(N);
	if (!Loc || Loc->getLine() < 1)
		return NULL;

	return Loc;
}

/// Print out source code information to facilitate manual analyses.
void printSourceCodeInfo(Value *V, string Tag, string SrcRoot)
{
	Instruction *I = dyn_cast<Instruction>(V);
	if (!I)
		return;

	DILocation *Loc = getSourceLocation(I);
	if (!Loc)
		return;

	unsigned LineNo = Loc->getLine();
	std::string FN = getFileName(SrcRoot, Loc);
	string line = getSourceLine(FN, LineNo);
	FN = Loc->getFilename().str();
	// FN = FN.substr(FN.find('/') + 1);
	// FN = FN.substr(FN.find('/') + 1);

	while (line[0] == ' ' || line[0] == '\t')
		line.erase(line.begin());
	OP << " ["
	   << "\033[34m" << Tag << "\033[0m" << "] "
	   << FN
	   << " +" << LineNo
	   // #ifdef PRINT_SOURCE_LINE
	   << " "
	   << "\033[35m" << line << "\033[0m" << '\n';
	OP << *I
	   // #endif
	   << "\n";
}

// write src info to file
void WriteSourceInfoIntoFile(Value *V, string file_name, string SrcRoot)
{
	Instruction *I = dyn_cast<Instruction>(V);
	if (!I)
		return;
	DILocation *Loc = getSourceLocation(I);
	if (!Loc)
		return;
	unsigned LineNo = Loc->getLine();
	std::string FN = getFileName(SrcRoot, Loc);
	FN = Loc->getFilename().str();
	string srcInfo = FN + " +" + to_string(LineNo) + "\t";
	std::ofstream oFile;
	oFile.open(file_name, std::ios::out | std::ios::app);
	oFile << srcInfo;
	oFile.close();
}

void printSourceCodeInfo(Function *F, string Tag, string SrcRoot)
{

	DISubprogram *SP = F->getSubprogram();

	if (SP)
	{
		string FN = getFileName(SrcRoot, NULL, SP);
		string line = getSourceLine(FN, SP->getLine());
		while (line[0] == ' ' || line[0] == '\t')
			line.erase(line.begin());

		FN = SP->getFilename().str();
		// FN = FN.substr(FN.find('/') + 1);
		// FN = FN.substr(FN.find('/') + 1);

		OP << " ["
		   << "\033[34m" << Tag << "\033[0m" << "] "
		   << FN
		   << " +" << SP->getLine()
#ifdef PRINT_SOURCE_LINE
		   << " "
		   << F->getName()
		//<< "\033[35m" << line << "\033[0m"
#endif
		   << '\n';
	}
#ifdef PRINT_SOURCE_LINE
	else
	{
		OP << " ["
		   << "\033[34m" << "??" << "\033[0m" << "] "
		   //<< F->getParent()->getName()<<": "<<F->getName()<<'\n';
		   << F->getName() << '\n';
	}
#endif
}

void WriteSourceInfoIntoFile(Function *F, string file_name, string SrcRoot)
{
	DISubprogram *SP = F->getSubprogram();
	string srcInfo = "";
	std::ofstream oFile;
	oFile.open(file_name, std::ios::out | std::ios::app);
	if (SP)
	{
		string FN = getFileName(SrcRoot, NULL, SP);
		string ln = to_string(SP->getLine());
		srcInfo = FN + " +" + ln + "\t";
		oFile << srcInfo;
	}
	else
	{
		oFile << F->getName().str() << "\t";
	}
	oFile.close();
}

string getMacroInfo(Value *V, string SrcRoot)
{

	Instruction *I = dyn_cast<Instruction>(V);
	if (!I)
		return "";

	DILocation *Loc = getSourceLocation(I);
	if (!Loc)
		return "";

	unsigned LineNo = Loc->getLine();
	std::string FN = getFileName(SrcRoot, Loc);
	string line = getSourceLine(FN, LineNo);
	FN = Loc->getFilename().str();
	const char *filename = FN.c_str();
	filename = strchr(filename, '/') + 1;
	filename = strchr(filename, '/') + 1;
	int idx = filename - FN.c_str();

	while (line[0] == ' ' || line[0] == '\t')
		line.erase(line.begin());

	string macro = extractMacro(line, I);

	// clean up the ending and whitespaces
	macro.erase(std::remove(macro.begin(), macro.end(), ' '), macro.end());
	unsigned length = 0;
	for (auto it = macro.begin(), e = macro.end(); it != e; ++it)
		if (*it == ')' || *it == ';' || *it == ',')
		{
			macro = macro.substr(0, length);
			break;
		}
		else
		{
			++length;
		}

	return macro;
}

/// Get source code information of this value
void getSourceCodeInfo(Value *V, string &file,
					   unsigned &line)
{
	file = "";
	line = 0;

	auto I = dyn_cast<Instruction>(V);
	if (!I)
		return;

	MDNode *N = I->getMetadata("dbg");
	if (!N)
		return;

	DILocation *Loc = dyn_cast<DILocation>(N);
	if (!Loc || Loc->getLine() < 1)
		return;

	file = Loc->getFilename().str();
	line = Loc->getLine();
}

int8_t getArgNoInCall(CallInst *CI, Value *Arg)
{

	int8_t Idx = 0;
	for (auto AI = CI->arg_begin(), E = CI->arg_end();
		 AI != E; ++AI)
	{
		if (*AI == Arg)
		{
			return Idx;
		}
		++Idx;
	}
	return -1;
}

Argument *getParamByArgNo(Function *F, int8_t ArgNo)
{

	if (ArgNo >= F->arg_size())
		return NULL;

	int8_t idx = 0;
	Function::arg_iterator ai = F->arg_begin();
	while (idx != ArgNo)
	{
		++ai;
		++idx;
	}
	return ai;
}

void LoadElementsStructNameMap(
	vector<pair<Module *, StringRef>> &Modules)
{

	for (auto M : Modules)
	{
		for (auto STy : M.first->getIdentifiedStructTypes())
		{
			assert(STy->hasName());
			if (STy->isOpaque())
				continue;

			string strSTy = structTyStr(STy);
			elementsStructNameMap[strSTy].insert(STy->getName());
		}
	}
}

void cleanString(string &str)
{
	// process string
	// remove c++ class type added by compiler
	size_t pos = str.find("(%class.");
	if (pos != string::npos)
	{
		// regex pattern1("\\(\\%class\\.[_A-Za-z0-9]+\\*,?");
		regex pattern("^[_A-Za-z0-9]+\\*,?");
		smatch match;
		string str_sub = str.substr(pos + 8);
		if (regex_search(str_sub, match, pattern))
		{
			str.replace(pos + 1, 7 + match[0].length(), "");
		}
	}
	string::iterator end_pos = remove(str.begin(), str.end(), ' ');
	str.erase(end_pos, str.end());
}

// #define HASH_SOURCE_INFO
string funcTypeString(FunctionType *FTy)
{

	string output;
	for (FunctionType::param_iterator pi = FTy->param_begin();
		 pi != FTy->param_end(); ++pi)
	{
		Type *PTy = *pi;
		string sig;
		raw_string_ostream rso(sig);
		PTy->print(rso);
		output += rso.str();
		// output += to_string(PTy->getTypeID());
		// output += ",";
	}
	return output;
}

size_t funcHash(Function *F, bool withName)
{

	hash<string> str_hash;
	string output;

#ifdef HASH_SOURCE_INFO
	DISubprogram *SP = F->getSubprogram();

	if (SP)
	{
		output = SP->getFilename();
		output = output + to_string(uint_hash(SP->getLine()));
	}
	else
	{
#endif
		string sig;
		raw_string_ostream rso(sig);
		FunctionType *FTy = F->getFunctionType();
		FTy->print(rso);
		output = rso.str();
		// output = funcTypeString(FTy);

		if (withName)
			output += F->getName();
#ifdef HASH_SOURCE_INFO
	}
#endif
	// process string
	cleanString(output);

	return str_hash(output);
}

size_t callHash(CallInst *CI)
{

	CallBase *CB = dyn_cast<CallBase>(CI);
	// Value *CO = CI->getCalledOperand();
	// if (CO) {
	//	Function *CF = dyn_cast<Function>(CO);
	//	if (CF)
	//		return funcHash(CF);
	// }
	hash<string> str_hash;
	string sig;
	raw_string_ostream rso(sig);
	FunctionType *FTy = CB->getFunctionType();
	FTy->print(rso);
	string strip_str = rso.str();
	// string strip_str = funcTypeString(FTy);
	cleanString(strip_str);

	return str_hash(strip_str);
}

string structTyStr(StructType *STy)
{
	string ty_str;
	string sig;
	for (auto Ty : STy->elements())
	{
		ty_str += to_string(Ty->getTypeID());
	}
	return ty_str;
}

void structTypeHash(StructType *STy, set<size_t> &HSet)
{
	hash<string> str_hash;
	string sig;
	string ty_str;

	// TODO: Use more but reliable information
	// FIXME: A few cases may not even have a name
	if (STy->hasName())
	{
		ty_str = STy->getName().str();
		HSet.insert(str_hash(ty_str));
	}
	else
	{
		string sstr = structTyStr(STy);
		if (elementsStructNameMap.find(sstr) != elementsStructNameMap.end())
		{
			for (auto SStr : elementsStructNameMap[sstr])
			{
				ty_str = SStr.str();
				HSet.insert(str_hash(ty_str));
			}
		}
	}
}

size_t typeHash(Type *Ty)
{
	hash<string> str_hash;
	string sig;
	string ty_str;

	if (StructType *STy = dyn_cast<StructType>(Ty))
	{
		// TODO: Use more but reliable information
		// FIXME: A few cases may not even have a name
		if (STy->hasName())
		{
			ty_str = STy->getName().str();
		}
		else
		{
			string sstr = structTyStr(STy);
			if (elementsStructNameMap.find(sstr) != elementsStructNameMap.end())
			{
				ty_str = elementsStructNameMap[sstr].begin()->str();
			}
		}
	}
#ifdef SOUND_MODE
	else if (ArrayType *ATy = dyn_cast<ArrayType>(Ty))
	{

		// Compiler sometimes fails recoginize size of array (compiler
		// bug?), so let's just use the element type

		// Ty = ATy->getElementType();
		raw_string_ostream rso(sig);
		Ty->print(rso);
		ty_str = rso.str() + "[array]";
		string::iterator end_pos = remove(ty_str.begin(), ty_str.end(), ' ');
		ty_str.erase(end_pos, ty_str.end());
	}
#endif
	else
	{
		raw_string_ostream rso(sig);
		Ty->print(rso);
		ty_str = rso.str();
		string::iterator end_pos = remove(ty_str.begin(), ty_str.end(), ' ');
		ty_str.erase(end_pos, ty_str.end());
	}
	return str_hash(ty_str);
}

size_t hashIdxHash(size_t Hs, int Idx)
{
	hash<string> str_hash;
	return Hs + str_hash(to_string(Idx));
}

size_t typeIdxHash(Type *Ty, int Idx)
{
	return hashIdxHash(typeHash(Ty), Idx);
}

size_t strIntHash(string str, int i)
{
	hash<string> str_hash;
	// FIXME: remove pos
	size_t pos = str.rfind("/");
	return str_hash(str.substr(0, pos) + to_string(i));
}

size_t moduleTypeHash(Module *M, size_t TyH)
{
	hash<string> str_hash;
	return str_hash(M->getName().str() + to_string(TyH));
}

int64_t getGEPOffset(const Value *V, const DataLayout *DL)
{

	const GEPOperator *GEP = dyn_cast<GEPOperator>(V);

	int64_t offset = 0;
	const Value *baseValue = GEP->getPointerOperand()->stripPointerCasts();
	// If we have yet another nested GEP const expr, accumulate its offset
	// The reason why we don't accumulate nested GEP instruction's offset is
	// that we aren't required to. Also, it is impossible to do that because
	// we are not sure if the indices of a GEP instruction contains all-consts or not.
	if (const ConstantExpr *cexp = dyn_cast<ConstantExpr>(baseValue))
		if (cexp->getOpcode() == Instruction::GetElementPtr)
		{
			// FIXME: this looks incorrect
			offset += getGEPOffset(cexp, DL);
		}
	Type *ptrTy = GEP->getSourceElementType();

	SmallVector<Value *, 4> indexOps(GEP->op_begin() + 1, GEP->op_end());
	// Make sure all indices are constants
	for (unsigned i = 0, e = indexOps.size(); i != e; ++i)
	{
		if (!isa<ConstantInt>(indexOps[i]))
			indexOps[i] = ConstantInt::get(Type::getInt32Ty(ptrTy->getContext()), 0);
	}
	offset += DL->getIndexedOffsetInType(ptrTy, indexOps);
	return offset;
}
