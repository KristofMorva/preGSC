#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <map>
#include <set>
//#include <time.h> // DEBUG

#ifdef _WIN32
	#include <direct.h>
	#include <windows.h>
	#include "dirent.h"
#else
	#include <cstdlib>
	#include <algorithm>
	#include <dirent.h>
	#include <sys/stat.h>
#endif

/*
Use spawnStruct(), it is passed by reference

class className
{
	constructor(p1, p2)
	{
		self.value = p1 * p2;
	}
	measure()
	{
		return self.value - 1;
	}
}

test := className(4, weaponid);			===========>			test := spawnStruct(); test className_constructor(4, weaponid);
cls->func(params)						===========>			[[level.__clsf[self.__clsid + "_func"]]](cls, params);
sth = test.value;

//map<string, string> classVars; varName -> className
*/

using namespace std;

set<string> functions;

enum errors { ERR_EOF, ERR_EXPECT, ERR_CUSTOM };
enum blocks { BLOCK_BRACKETS, BLOCK_FIRST, BLOCK_SEMICOLON };

struct Inherit
{
	vector<string> params;
	vector<pair<size_t, size_t>> list; // Pos, Param ID
	string body;
};

set<string> def;
set<string> globaldef;
map<string, size_t> ignoref;
map<string, string> par; // Default parameters
map<string, string> globalref; // Global references for temp
map<string, Inherit> inherit;
map<string, string> ext; // Externs
Inherit* searchparam = nullptr;
bool parentIsGlobal;
bool debug = false;
bool ispause = true;
char zeroarrays = 0;
bool bglobalref = false;
size_t searchcount;
size_t holders = SIZE_MAX;
size_t depth;
string holderelem;
string infold = "source";
string outfold = "maps";
string file;

void errorColor(bool err = true)
{
	#ifdef _WIN32
		if (err)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
		else
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
	#else
		if (err)
			cout << "\033[01;31m";
		else
			cout << "\033[0m";
	#endif
}

void pause()
{
	// Free up what we can
	//s.clear();
	//classVars.clear();
	ignoref.clear();
	par.clear();
	def.clear();
	inherit.clear();
	functions.clear();
	ext.clear();

	cout << endl << endl << "Press ENTER to exit...";
	getchar();
}

void warning(const string &t)
{
	errorColor();
	cout << endl << endl << "WARNING: " << t;
	errorColor(false);
	pause();
	exit(EXIT_FAILURE);
}

void error(const errors e, const size_t last, const string &s, size_t &i, const size_t n, const string &s1 = "", const string &s2 = "end of file")
{
	errorColor();
	cout << endl << endl << "ERROR" << (!file.empty() ? " in imported file '" + file + "'" : "") << ": ";
	if (e == ERR_EOF)
		cout << "Unexpected end of file";
	else if (e == ERR_EXPECT)
		cout << "Expected '" << s1 << "', but found '" << s2 << "'";
	else if (e == ERR_CUSTOM)
		cout << s1;

	// count() is not effective for getting row number, but for an error it is okay.
	size_t c = count(s.begin(), s.begin() + last, '\n') + 1;

	if (i == last)
		cout << " at line " << c << ".";
	else
		cout << " at line " << count(s.begin() + last, s.begin() + i, '\n') + c << ", started from line " << c << ".";

	errorColor(false);
	pause();
	exit(EXIT_FAILURE);
}

// Return the whole file
string loadFile(const string &t, bool error = false)
{
	ifstream f(t.c_str());
	if (f.is_open())
	{
		stringstream ss;
		ss << f.rdbuf();
		f.close();
		return ss.str();
	}
	else if (error)
	{
		errorColor();
		cout << endl << endl << "ERROR: Imported file '" << t << "' not found!";
		errorColor(false);
		pause();
		exit(EXIT_FAILURE);
	}
	else return "";
}

bool isNumber(const string &v)
{
	for (size_t j = 0; j < v.size(); ++j)
	{
		if (!isdigit(v[j]))
		{
			return false;
		}
	}
	return true;
}

// Token
bool isVar(const char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isVar(const string &v)
{
	for (size_t j = 0; j < v.size(); ++j)
	{
		if (!isVar(v[j]))
		{
			return false;
		}
	}
	return true;
}

// Operator
bool isOp(const char& c)
{
	return c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' || c == '|' || c == '&' || c == '%'; // c == '=' || 
}

bool match(const string &t, const size_t len, const string &s, size_t &i)
{
	for (size_t j = 0; j <= len; ++j)
	{
		if (s[i - j + 1] != t[len - j])
			return false;
	}
	return true;
}

void tillMatch(const string t, const size_t last, string &temp, const string &s, size_t &i, const size_t n)
{
	const size_t len = t.size() - 1;
	i += len;
	
	if (debug)
		while (++i < n)
		{
			if (s[i - len] == '\n')
				temp += '\n';

			if (match(t, len, s, i))
				break;
		}
	else
		while (++i < n && !match(t, len, s, i));

	if (++i >= n)
		error(ERR_EXPECT, last, s, i, n, t);
}

void tillMatch(const char t, const size_t last, string &temp, const string &s, size_t &i, const size_t n)
{
	if (debug)
		while (++i < n && s[i] != t) { if (s[i] == '\n') temp += '\n'; }
	else
		while (++i < n && s[i] != t);

	if (++i >= n)
		error(ERR_EXPECT, last, s, i, n, string(1, t));
}

void readSpace(const size_t last, string &temp, const string &s, size_t &i, const size_t n, const bool checklast = true) // , const bool pres = true
{
	/*if (debug)
	{
		while (isspace(s[i]) && ++i < n)
		{
			if (pres)
				temp += s[i - 1];
			else if (s[i - 1] == '\n')
				temp += '\n';
		}
	}
	else*/

	do
	{
		while (isspace(s[i]) && ++i < n)
		{
			if (debug && s[i - 1] == '\n')
				temp += '\n';

			if (searchparam != nullptr && searchcount != SIZE_MAX && s[i - 1] == '\n')
				++searchcount;
		}

		if (s[i] == '/' && i + 1 < n)
		{
			if (s[i + 1] == '/')
			{
				++i;
				while (++i < n && s[i] != '\n');

				if (debug)
					temp += '\n';

				++i;
			}
			else if (s[i + 1] == '*')
			{
				++i;
				tillMatch("*/", last, temp, s, i, n);

				++i;
			}
			else break;
		}
		else break;
	}
	while (true);

	if (i == n && checklast)
		error(ERR_EOF, last, s, i, n);
}

string readVar(const size_t last, const string &err, const string &s, size_t &i, const size_t n, const bool checklast = true)
{
	string elem;
	while (isVar(s[i]))
	{
		elem += s[i];

		if (++i == n)
		{
			if (checklast)
				error(ERR_EOF, last, s, i, n);

			break;
		}
	}

	if (elem.empty())
		error(ERR_EXPECT, last, s, i, n, err, string(1, s[i]));

	return elem;
}

void mustBeNewLine(string &temp, const string &s, size_t &i, const size_t n)
{
	if (i != n)
	{
		if (s[i] != '\n')
			error(ERR_EXPECT, i, s, i, n, "new line", string(1, s[i]));

		if (debug)
			temp += '\n';
	}
}

string getConstant(const size_t last, string &temp, const string &s, size_t &i, const size_t n)
{
	readSpace(last, temp, s, i, n);
	string t = readVar(last, "constant name", s, i, n, false);

	if (i != n)
	{
		if (s[i] != '\n')
			error(ERR_EXPECT, last, s, i, n, "new line", string(1, s[i]));

		if (debug)
			temp += '\n';
	}

	return t;
}

void writeFunction(ofstream &g, const string &name, const string &func)
{
	if (functions.find(name) != functions.end())
	{
		if (debug)
			g << endl;

		g << func;
	}
}

void newRef(map<string, string> &refs, string &elem, string &val)
{
	if (bglobalref)
		globalref[elem] = val;
	else
		refs[elem] = val;
}

// Evaluate the code
// 'last' is the last block start
// 'i' is the first character to evaluate
// 'refs' contains the reference variables
// 'newblock' will return after found a separator character (; : =)
string eval(const size_t last, map<string, string> refs, const string &s, size_t &i, const size_t n, const blocks newblock = BLOCK_BRACKETS)
{
	string temp, part;
	
	bool kwfound = false; // We need it, because there can be \s chars before a variable/constant
	size_t cc = SIZE_MAX;

	while (i < n)
	{
		if (s[i] == '/' && i + 1 < n && s[i + 1] == '/')
		{
			++i;
			while (++i < n && s[i] != '\n');

			if (debug)
				part += '\n';
		}
		else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*')
		{
			++i;
			tillMatch("*/", last, temp.empty() ? part : temp, s, i, n);
		}
		else if (s[i] == '"')
		{
			kwfound = false;
			temp += '"';
			bool slash = false;
			while (++i < n && (s[i] != '"' || (slash && s[i - 1] == '\\')))
			{
				if (slash)
					slash = false;
				else if (s[i] == '\\')
					slash = true;
				temp += s[i];
			}

			if (i == n)
				error(ERR_EXPECT, last, s, i, n, "\"");

			temp += '"';
			//kwfound = true;
		}
		else if (s[i] == '#' && (!i || ((s[i - 1] != '/') && (i + 1 >= n || s[i + 1] != '/'))))
		{
			kwfound = false;

			++i;
			string t = readVar(i, "preprocessor directive", s, i, n, false);
			if (t == "include" || t == "using_animtree")
			{
				if (!temp.empty() && temp[temp.size() - 1] != '\n')
					temp += '\n';

				temp += '#' + t;

				if (i != n)
				{
					do
						temp += s[i];
					while (++i < n && s[i] != '\n');
					temp += '\n';
				}
			}
			else if (t == "define")
			{
				def.insert(getConstant(i, temp, s, i, n));
			}
			else if (t == "import")
			{
				readSpace(last, temp, s, i, n);
				string v;
				do
					v += s[i];
				while (++i < n && s[i] != '\n');
		
				t = loadFile(infold + '/' + v, true);
				if (!t.empty())
				{
					bool olddebug = debug;
					debug = false;
					size_t k = 0;
					file = v;
					if (bglobalref)
					{
						eval(last, globalref, t, k, t.size());
					}
					else
					{
						bglobalref = true;
						eval(last, globalref, t, k, t.size());
						bglobalref = false;
						refs.insert(globalref.begin(), globalref.end());
					}
					globalref.clear();
					file.clear();
					debug = olddebug;
				}

				if (debug)
					temp += '\n';
			}
			else if (t == "undef")
			{
				def.erase(getConstant(i, temp, s, i, n));
			}
			else if (t == "ifdef" || t == "ifndef")
			{
				if ((t == "ifdef") == (def.find(getConstant(i, temp, s, i, n)) != def.end()))
				{
					cc = i; // Only for match checking
				}
				else
				{
					do
					{
						tillMatch('#', i, temp, s, i, n);
						
						t = readVar(i, "preprocessor directive", s, i, n, false);

						if (t == "else")
							cc = i;
						else if (t != "endif")
							continue;
					}
					while (false);

					mustBeNewLine(temp, s, i, n);
				}
			}
			else if (t == "else")
			{
				if (cc == SIZE_MAX)
					error(ERR_CUSTOM, i, s, i, n, "Unexpected #else directive: no matching #ifdef or #ifndef found");

				mustBeNewLine(temp, s, i, n);

				tillMatch("#endif", i, temp, s, i, n);
			}
			else if (t == "endif")
			{
				if (cc != SIZE_MAX)
					cc = SIZE_MAX;
				else
					error(ERR_CUSTOM, i, s, i, n, "Unexpected #endif directive: no matching #ifdef or #ifndef found");

				mustBeNewLine(temp, s, i, n);
			}
			else continue;
		}
		else if (s[i] == ')' || s[i] == ']' || s[i] == '}')
		{
			char c = s[last] == '(' ? ')' : (s[last] == '[' ? ']' : (s[last] == '{' ? '}' : '\0'));
			if (c == '\0')
				error(ERR_CUSTOM, 0, s, i, n, "Unexpected '" + string(1, s[i]) + "', opening bracket not found");

			if (s[i] != c)
				error(ERR_EXPECT, last, s, i, n, string(1, c), string(1, s[i]));
			
			if (newblock == BLOCK_FIRST)
				return part + temp;
			else
				return part + temp + s[i];
		}
		else if (s[i] == '(' || s[i] == '[' || s[i] == '{')
		{
			bool l = parentIsGlobal;
			parentIsGlobal = !last;

			//kwfound = s[i] != '{';
			if (s[i] == '{')
			{
				part += temp + s[i];
				temp = "";
				if (!par.empty())
				{
					for (map<string, string>::const_iterator it = par.begin(); it != par.end(); ++it)
					{
						part += "if(!isDefined(" + it->first + "))" + it->first + "=" + it->second + ";";
					}
					par.clear();
				}
				part += eval(i++, refs, s, i, n);
			}
			else
			{
				temp += s[i]; // eval() may be evaluated sooner if "s[i] + eval()" is used
				temp += eval(i++, refs, s, i, n);
			}
			parentIsGlobal = l;
			kwfound = false;
		}
		else if (s[i] == '=')
		{
			if (i + 1 < n && s[i + 1] == '=') // Equal
			{
				temp += "==";
				++i;
				//kwfound = true;
			}
			else if (i && isOp(s[i - 1])) // Not setting value
			{
				temp += '=';
				//kwfound = true;
			}
			else // Setting a value -> Breakpoint (like ;)
			{
				// Default parameters
				if (parentIsGlobal && s[last] == '(')
				{
					if (!kwfound)
						error(ERR_CUSTOM, last, s, i, n, "Default parameter has no name");

					++i;
					par[temp] = eval(last, refs, s, i, n, BLOCK_FIRST);

					if (s[i] == ')')
						continue;
					else
						part += temp + s[i];
				}
				else if (newblock == BLOCK_FIRST)
				{
					return part + temp;
				}
				else
				{
					part += temp + '=';
				}
				temp = "";
				//kwfound = false;
			}
			kwfound = false;
		}
		else if (s[i] == '&' && temp.empty() && i + 1 < n && s[i + 1] != '"' && s[i + 1] != '\'') // Reference + No localized string
		{
			++i;
			string t = readVar(last, "reference variable name", s, i, n);

			readSpace(last, temp, s, i, n);
			if (s[i] == '=')
			{
				++i;
				readSpace(last, temp, s, i, n);
				string q = eval(last, refs, s, i, n, BLOCK_FIRST);

				if (s[i] == ';')
					newRef(refs, t, q);
				else
					error(ERR_EXPECT, last, s, i, n, ";");
			}
			else error(ERR_EXPECT, last, s, i, n, "=");
		}
		else if (newblock == BLOCK_SEMICOLON && s[i] == ';') // One-line of foreach or do-while without { }
		{
			return part + temp + s[i];
		}
		else if (newblock == BLOCK_FIRST && (s[i] == ';' || s[i] == ',')) // For example 'foreach', 'makearray' and 'makemap'
		{
			return part + temp;
		}
		else if (newblock == BLOCK_FIRST && s[i] == ':') // (cond ? true : false), 'makemap'
		{
			if (i < n && s[i + 1] == ':')
			{
				++i;
				temp += "::";
				//kwfound = true;
				kwfound = false;
			}
			else return part + temp;
		}
		else if (s[i] == '\'')
		{
			temp += '"';
			kwfound = false;
		}
		else if (s[i] == ',' && s[last] == '{') // Comma in a curly block is only for setting multiple variables
		{
			vector<string> v;
			v.push_back(temp);
			do
			{
				++i;
				readSpace(last, part, s, i, n);
				temp = eval(last, refs, s, i, n, BLOCK_FIRST);

				if (temp.empty())
					error(ERR_EXPECT, last, s, i, n, "variable name", string(1, s[i]));

				v.push_back(temp);
			}
			while (s[i] == ',');

			if (s[i] != '=')
				error(ERR_EXPECT, last, s, i, n, "=' or ',", string(1, s[i]));

			++i;
			readSpace(last, part, s, i, n);
			temp = eval(last, refs, s, i, n, BLOCK_FIRST);

			if (s[i] != ';')
				error(ERR_EXPECT, last, s, i, n, ";", string(1, s[i]));

			if (temp.find('(') == string::npos) // Not a function as value
			{
				for (size_t j = 0; j < v.size(); ++j)
					part += v[j] + '=' + temp + ';';
			}
			else // Every variable will get the value from the first one, so no need to eval the function more time
			{
				part += v[0] + '=' + temp + ';';

				for (size_t j = 1; j < v.size(); ++j)
					part += v[j] + '=' + v[0] + ';';
			}
			temp = "";
			kwfound = false;
		}
		else if (s[i] == ';' || s[i] == ',') // Comma for default params
		{
			part += temp + s[i];
			temp = "";
			kwfound = false;
		}
		else if (s[i] == '?')
		{
			++i;
			string t = eval(last, refs, s, i, n, BLOCK_FIRST);
			if (i < n)
			{
				if (s[i] == ':')
				{
					++i;
					t += ',' + eval(last, refs, s, i, n, BLOCK_FIRST);

					if (i == n)
						error(ERR_EOF, last, s, i, n);
				}
				functions.insert("sif");
				temp = "__sif(" + temp + "," + t + ")"; //  + s[i]
				//kwfound = true;
				kwfound = false;
				continue; // Maybe a closing bracket was the last
			}
			else error(ERR_EOF, last, s, i, n);
		}
		else if (s[i] == '$')
		{
			++i;
			string t = readVar(last, "hexadecimal color value", s, i, n);
			if (t.size() != 6)
				error(ERR_CUSTOM, last, s, i, n, "hexadecimal color value is expected to be 6 character long, but it is " + to_string(t.size()));
			
			size_t q;
			size_t r = stoul(t, &q, 16);
			if (q != 6)
				error(ERR_CUSTOM, last, s, i, n, t + " is not a valid hexadecimal value (invalid character: '" + to_string(t[q - 1]) + "')");

			t = "";
			for (q = 0; q < 3; ++q)
			{
				if (q)
					t = ',' + t;

				t = to_string(((r % 256) / 255.0)) + t;
				r /= 256;
			}
			temp += '(' + t + ')';
			continue;
		}
		else if (isVar(s[i]))
		{
			//do
			//{
				string v;
				do
					v += s[i];
				while (++i < n && isVar(s[i]));

				if (i < n)
				{
					bool proc = true;
					size_t b = temp.size();
					if ((!b || (temp[b - 1] != '.' && temp[b - 1] != ':' && temp[b - 1] != '\\' && (i + 1 == n || s[i + 1] != '\\')))) // Not field object, not under another namespace, and not a file path
					{
						if (v == "do")
						{
							part += temp + "while(true){";
							temp = "";
							readSpace(last, part, s, i, n);

							if (s[i] == '{')
							{
								part += eval(i++, refs, s, i, n);
								part.pop_back();
							}
							else
								part += eval(i, refs, s, i, n, BLOCK_SEMICOLON);
							
							++i;
							readSpace(last, part, s, i, n);

							if (readVar(last, "while", s, i, n) != "while")
								error(ERR_EXPECT, last, s, i, n, "while", v);

							readSpace(last, part, s, i, n);

							if (s[i] != '(')
								error(ERR_EXPECT, last, s, i, n, "(", string(1, s[i]));

							v = eval(i++, refs, s, i, n); // Condition
							part += "if(!(" + v + ")break;}"; // One closing bracket is already in 'v'
							
							++i;
							readSpace(last, part, s, i, n);
							if (s[i] != ';')
								error(ERR_EXPECT, last, s, i, n, ";", string(1, s[i]));
							
							++i;
						}
						else if (v == "foreach")
						{
							if (zeroarrays != -1)
							{
								bool washold = holders != SIZE_MAX;
								string oldholder = holderelem;

								// Separate temp and part to be able to insert the loop header between them.
								part += temp;
								temp = "";

								// Get '('
								readSpace(last, part, s, i, n);
								if (s[i] != '(')
									error(ERR_EXPECT, last, s, i, n, "(", string(1, s[i]));
							
								// Get element name
								size_t last_new = i;
								++i;
								readSpace(last_new, part, s, i, n);
								string elem = readVar(last_new, "array element name", s, i, n);
							
								// Get 'in'
								readSpace(last_new, part, s, i, n);
								v = readVar(last_new, "in", s, i, n);
								if (v != "in" && v != "as")
									error(ERR_EXPECT, last_new, s, i, n, "in' or 'as", v);

								// Get array name or key name
								readSpace(last_new, part, s, i, n);
								string k = eval(last_new, refs, s, i, n, BLOCK_FIRST); // If eval returned, then something is found for sure

								if (s[i] == ':')
								{
									// Get array name if key found
									++i;
									readSpace(last_new, part, s, i, n);
									v = eval(last_new, refs, s, i, n, BLOCK_FIRST);
								}
								else
								{
									v = k;
									k.clear();
								}

								// Parameters
								string vfrom;
								string vsize;
								string vkeys;

								// From
								if (s[i] == ';')
								{
									++i;
									readSpace(last_new, part, s, i, n);
									vfrom = eval(last_new, refs, s, i, n, BLOCK_FIRST);
									readSpace(last_new, part, s, i, n);

									// Size
									if (s[i] == ';')
									{
										++i;
										readSpace(last_new, part, s, i, n);
										vsize = eval(last_new, refs, s, i, n, BLOCK_FIRST);
										readSpace(last_new, part, s, i, n);
									}
								}

								if (s[i] != ')')
									error(ERR_EXPECT, last_new, s, i, n, ")", string(1, s[i]));

								if (++i == n)
									error(ERR_EOF, last_new, s, i, n);
							
								string start; // We should store in this the opening '{' char and the whitespaces around it to keep '{' in its proper row (so only nicer generated code). Without debug mode, every 'start' can be replaced to 'temp' (+ append '{' after 'for').
								readSpace(last_new, start, s, i, n);

								blocks till;
								if (s[i] == '{')
								{
									till = BLOCK_BRACKETS;
									last_new = i;
									start += '{';
									++i;
									readSpace(last_new, start, s, i, n);
								}
								else
								{
									till = BLOCK_SEMICOLON;
									last_new = last;
								}
								//if (s[i] != '{')
									//error(ERR_EXPECT, last_new, s, i, n, "{", string(1, s[i]));
							
								const string id = to_string(depth++);
								holderelem = "__e" + id; // We can overwrite the parent one, because we won't replace that's elems anyways
								holders = 0;
								newRef(refs, elem, holderelem); // Array element name will be handled as a reference; but we don't know yet if we should store array[i] or a pointer to the array, so use a placeholder for that
								size_t ts = temp.size();
								temp += eval(last_new, refs, s, i, n, till);
								refs.erase(elem);

								bool keys = vfrom.size() && vfrom[0] == '@';
								string key;
								if (keys)
								{
									if (vfrom.size() != 1)
										key = vfrom.substr(1);

									vfrom = "";
								}
								else
									keys = vfrom.empty() && !zeroarrays;
							
								if (keys && key.empty())
									key = "__k" + id;

								const bool pos = !vsize.empty() && vsize[0] == '+';

								if (pos)
									vsize.erase(0, 1);

								const bool sizedef = !vsize.empty();
								const bool sizevar = sizedef && isVar(vsize);
								const bool sizeconst = sizevar && isNumber(vsize);
								const bool fromdef = !vfrom.empty() && vfrom != "0";
								const bool fromvar = fromdef && isVar(vfrom);
								const bool fromconst = fromvar && isNumber(vfrom);
								const bool isc = (!sizevar || fromdef) && (!sizeconst || !fromconst);
								const bool iss = isc && fromdef && !fromvar;
								const bool var = isVar(v);
								const bool cond = !sizeconst && !pos;
								const string from = iss ? "__s" + id : vfrom;
								if (k.empty())
									k = "__i" + id;

								if (sizeconst && vsize == "0")
									error(ERR_CUSTOM, last_new, s, i, n, "Size of foreach cannot be 0");

								if (cond && !isc)
									part += "if(" + vsize + (fromdef ? "!=" + from : "") + "){";

								// Check if the array name is complex
								if (!var)
								{
									part += "__v" + id + '=' + v + ';';
									v = "__v" + id;
								}

								part += (isc ? (iss ? from + '=' + vfrom + ';' : "") + "__c" + id + '=' + (sizedef ? vsize + (fromdef ? '+' + from : "") : v + ".size" + (fromdef ? '+' + from : "")) + ';' + (cond && isc ? "if(__c" + id + (fromdef ? "!=" + from : "") + "){" : "") : "") + (keys ? key + "=getArrayKeys(" + v + ");" : "") + "for(" + k + '=' + (!fromdef ? "0" : from) + ";" + k + '<' + (!isc ? (sizeconst && fromconst ? to_string(atoi(vfrom.c_str()) + atoi(vsize.c_str())) : vsize) : "__c" + id) + ";" + k + "++)" + start;

								// Check the count of elements used in the loop
								if (holders)
								{
									// With a logical script, code should always reach this point - no use of foreach() without using the element
									if (holders < 3 && (!keys || holders == 1)) // If 1 or (2 and not using keys), then use __v vars as placeholders and replace them back (it's because pointer is only faster than direct access if used more times); currently it's not perfect, because we don't check if any of them are in an if() or not
									{
										size_t l = holderelem.size();
										for (size_t j = 0; j < holders; ++j)
										{
											ts = temp.find(holderelem, ts);
											temp.replace(ts, l, v + "[" + (keys ? key + "[" + k + "]" : k) + "]");
										}
									}
									else
									{
										part += holderelem + "=" + v + "[" + (keys ? key + "[" + k + "]" : k) + "];";
									}
								}

								if (washold)
								{
									holders = SIZE_MAX - 1; // SIZE_MAX - 1 means, that loop in a foreach; in this extreme case, there is a foreach in a foreach
									holderelem = oldholder;
								}
								else
									holders = SIZE_MAX;

								part += temp + k + "=undefined;";
							
								if (keys)
									part += key + "=undefined;";

								if (isc)
								{
									if (cond)
										part += '}';

									part += "__c" + id + "=undefined;";

									if (iss)
										part += "__s" + id + "=undefined;";
								}

								if (!var)
									part += "__v" + id + "=undefined;";
							
								if (!isc && cond)
									part += '}';

								++i;
								temp = "";
							}
							else proc = false;
						}
						else if (v == "inline")
						{
							readSpace(last, temp, s, i, n);
							
							v = readVar(last, "inline function name", s, i, n);
							inherit[v] = Inherit();
							Inherit &data = inherit[v];

							readSpace(last, temp, s, i, n);

							int last_new = i;
							if (s[i] != '(')
								error(ERR_EXPECT, last, s, i, n, "(", string(1, s[i]));

							++i;
							if (s[i] != ')')
							{
								for (int j = 0; true; ++j)
								{
									data.params.push_back(readVar(last_new, "parameter name", s, i, n));

									if (s[i] == ')')
										break;
									
									++i;
									readSpace(last, temp, s, i, n);
								}
							}
							++i;
							readSpace(last, temp, s, i, n);

							last_new = i;
							if (s[i] != '{')
								error(ERR_EXPECT, last_new, s, i, n, "{", string(1, s[i]));

							++i;
							//readSpace(last_new, temp, false);

							// Inherited functions must be one line
							searchcount = debug ? 0 : SIZE_MAX;
							bool olddebug = debug;
							debug = false;
							searchparam = &data;
							//data->pos = i;
							data.body = eval(last_new, refs, s, i, n);
							data.body.pop_back(); // Remove last '}'
							searchparam = nullptr;
							debug = olddebug;

							if (debug)
								temp += string(searchcount, '\n');
							
							// Let's search the positions
							const string &t = data.body;
							size_t k = data.list.size() - 1;
							for (size_t y = t.size() - 1; k != SIZE_MAX; --y) //  && y != SIZE_MAX
							{
								if (t[y] == '"')
								{
									--y;
									while (t[y] != '"')
										--y;
									--y;
								}
								if (isVar(t[y]))
								{
									string x = string(1, t[y]);
									while (--y != SIZE_MAX && isVar(t[y]))
									{
										x += t[y];
									}
									if (y == SIZE_MAX || (t[y] != '.' && t[y] != ':'))
									{
										string &z = data.params[data.list[k].second];
										if (x.size() == z.size() && equal(x.begin(), x.end(), z.rbegin()))
										{
											data.list[k].first = y + 1;
											--k;
										}
									}
									if (y == SIZE_MAX)
										break;
								}
							}

							++i;
						}
						else if (v == "makearray")
						{
							if (!kwfound)
								error(ERR_CUSTOM, last, s, i, n, "makearray() requires a variable name");

							if (temp.find('\n') != string::npos)
								error(ERR_CUSTOM, last, s, i, n, "The name of the array mustn't contain any new line character");

							//temp.pop_back(); // Remove last whitespace
							v = temp;
							temp += "=[]";
							readSpace(last, temp, s, i, n);
							
							int last_new = i;
							if (s[i] != '(')
								error(ERR_EXPECT, last_new, s, i, n, "(", string(1, s[i]));
							
							++i;
							readSpace(last, temp, s, i, n);
							if (s[i] != ')')
							{
								for (int j = 0; true; ++j)
								{
									temp += ";" + v + "[" + to_string(j) + "]=" + eval(last_new, refs, s, i, n, BLOCK_FIRST);

									if (s[i] == ')')
										break;
									
									++i;
									readSpace(last, temp, s, i, n);
								}
							}

							++i;
							// Let the code handle the last ';'
						}
						else if (v == "makemap")
						{
							if (!kwfound)
								error(ERR_CUSTOM, last, s, i, n, "makemap() requires a variable name");

							if (temp.find('\n') != string::npos)
								error(ERR_CUSTOM, last, s, i, n, "The name of the array mustn't contain any new line character");

							//temp.pop_back(); // Remove last whitespace
							v = temp;
							temp += "=[]";
							readSpace(last, temp, s, i, n);
							
							int last_new = i;
							if (s[i] != '(')
								error(ERR_EXPECT, last_new, s, i, n, "(", string(1, s[i]));
							
							++i;
							readSpace(last, temp, s, i, n);
							if (s[i] != ')')
							{
								do
								{
									string q = eval(last_new, refs, s, i, n, BLOCK_FIRST);

									if (s[i] != ':')
										error(ERR_EXPECT, last_new, s, i, n, ":", string(1, s[i]));

									++i;
									readSpace(last, temp, s, i, n);
									temp += ";" + v + "[" + q + "]=" + eval(last_new, refs, s, i, n, BLOCK_FIRST);

									if (s[i] == ')')
										break;
									
									++i;
									readSpace(last, temp, s, i, n);
								}
								while (true);
							}

							++i;
							// Let the code handle the last ';'
						}
						else if (v == "print_r")
						{
							temp += "__par(";
							readSpace(last, temp, s, i, n);

							if (s[i] != '(')
								error(ERR_EXPECT, last, s, i, n, "(", string(1, s[i]));

							size_t last_new = i;
							++i;
							v = eval(last_new, refs, s, i, n, BLOCK_FIRST);
							if (s[i] != ')')
								error(ERR_EXPECT, last_new, s, i, n, ")", string(1, s[i]));

							temp += v + ",\"" + v + "\")";
							++i;

							functions.insert("par");
						}
						else if (v == "extern")
						{
							readSpace(last, temp, s, i, n);
							v = readVar(last, "Script type", s, i, n);

							readSpace(last, temp, s, i, n);
							if (s[i] != '{')
								error(ERR_EXPECT, last, s, i, n, "{", string(1, s[i]));

							++i;
							string w = eval(i - 1, refs, s, i, n);
							w.pop_back();
							if (ext.find(v) == ext.end())
							{
								ext[v] = w;
							}
							else
							{
								if (debug)
									ext[v] += '\n';

								ext[v] += w;
							}
							++i;
						}
						else if (v == "enum")
						{
							readSpace(last, temp, s, i, n);

							if (s[i] != '{')
								error(ERR_EXPECT, last, s, i, n, "{", string(1, s[i]));
							
							size_t last_new = i;

							++i;
							for (size_t j = 0;;++j)
							{
								readSpace(last_new, temp, s, i, n);
								refs[readVar(last_new, "constant name", s, i, n)] = to_string(j);
								readSpace(last_new, temp, s, i, n);

								if (s[i] == '}')
									break;

								if (s[i] != ',')
									error(ERR_EXPECT, last_new, s, i, n, ",", string(1, s[i]));

								++i;
							}
							++i;
						}
						else if (v == "elseif")
						{
							temp += "else if";
						}
						/*else if (!last && v == "class")
						{
							readSpace(last, temp, s, i, n);

							// Get classname
							string t = readVar(last, "class name", s, i, n);
							
							readSpace(last, temp, s, i, n);

							if (s[i] != '{')
								error(ERR_EXPECT, last, s, i, n, "{", string(1, s[i]));

							// TODO
						}*/
						else if (refs.find(v) != refs.end())
						{
							v = refs[v];
							if (holders != SIZE_MAX) // Foreach
							{
								if (v == holderelem || v.find(holderelem) != string::npos) // It is the elem, or a reference variables is using it
								{
									if (holders != SIZE_MAX - 1) // elem is used in a loop -> probably used at least 3 times (we can't determine)
										++holders;
								}
							}
							
							proc = false;
						}
						else if (inherit.find(v) != inherit.end())
						{
							readSpace(last, temp, s, i, n);

							int last_new = i;
							if (s[i] != '(')
								error(ERR_EXPECT, last_new, s, i, n, "(", string(1, s[i]));

							++i;
							vector<string> params;
							if (s[i] != ')')
							{
								do
								{
									readSpace(last, temp, s, i, n);
									params.push_back(eval(last_new, refs, s, i, n, BLOCK_FIRST));

									if (s[i] == ')')
										break;

									++i;
								}
								while (true);
							}
							
							++i;
							if (params.size() != inherit[v].params.size())
								error(ERR_CUSTOM, last_new, s, i, n, "Parameter count does not match for inline function '" + v + '\'');

							readSpace(last, temp, s, i, n);

							if (s[i] != ';')
								error(ERR_EXPECT, last, s, i, n, ";", string(1, s[i]));

							++i;
							
							part += temp;
							temp = inherit[v].body;
							for (size_t k = inherit[v].list.size() - 1; k != SIZE_MAX; --k)
							{
								temp.replace(inherit[v].list[k].first, inherit[v].params[inherit[v].list[k].second].size(), params[inherit[v].list[k].second]);
							}
							part += temp;
							temp = "";
						}
						else if (holders < SIZE_MAX - 1 && (v == "while" || v == "for")) // Foreach with a loop in that
						{
							holders = SIZE_MAX - 1;
							proc = false;
						}
						else if (v[0] == '0' && v[1] == 'x') // Hexadecimal
						{
							try
							{
								temp += to_string(stoul(v, nullptr, 16));
							}
							catch (const invalid_argument&)
							{
								error(ERR_CUSTOM, last, s, i, n, '\'' + v + "' is not a valid hexadecimal value");
							}
							catch (const out_of_range&)
							{
								error(ERR_CUSTOM, last, s, i, n, '\'' + v + "' is a too big hexadecimal value");
							}
						}
						else if (v[0] == '0' && v.size() > 1) // Octal
						{
							try
							{
								temp += to_string(stoul(v, nullptr, 8));
							}
							catch (const invalid_argument&)
							{
								error(ERR_CUSTOM, last, s, i, n, '\'' + v + "' is not a valid octal value");
							}
							catch (const out_of_range&)
							{
								error(ERR_CUSTOM, last, s, i, n, '\'' + v + "' is a too big octal value");
							}
						}
						else if (searchparam != nullptr)
						{
							for (size_t j = 0; j < searchparam->params.size(); ++j)
							{
								if (searchparam->params[j] == v)
								{
									searchparam->list.push_back(pair<size_t, size_t>(0, j)); // i - v.size() - searchparam->pos - 2 Sadly we don't know how many characters do we have in parent part and temp
									break;
								}
							}

							proc = false;
						}
						else proc = false;
					}
					else proc = false;

					if (!proc)
					{
						if (kwfound)
							temp += ' ';
						else
							kwfound = true;

						temp += v;

						/*if (!debug && isspace(s[i]))
						{
							while (++i < n && isspace(s[i]));
							if (isVar(s[i]))
								temp += ' ';
							else break;
						}
						else break;*/
					}
					else
					{
						kwfound = false;
					}
				}
				else
				{
					if (kwfound)
						temp += ' ';
					else
						kwfound = true;

					temp += v;
					//break;
				}
			//}
			//while (true);

			//kwfound = true;

			continue;
		}
		else if (!isspace(s[i]))
		{
			temp += s[i];
			//kwfound = true;
			kwfound = false;
		}
		else if (s[i] == '\n')
		{
			if (debug)
			{
				if (temp.empty())
					part += '\n';
				else
					temp += '\n';
			}
			else if (searchparam != nullptr && searchcount != SIZE_MAX)
			{
				++searchcount;
			}
		}
		++i;
	}

	if (last)
		error(ERR_CUSTOM, last, s, i, n, "Unclosed '" + string(1, s[last]) + '\'');

	if (cc != SIZE_MAX)
		error(ERR_CUSTOM, cc, s, i, n, "Unclosed preprocessor condition");

	return part + temp;
}

void makeDir(const string d)
{
	#ifdef _WIN32
		_mkdir(d.c_str());
	#else
		mkdir(d.c_str(), 0777);
	#endif
}

bool listFiles(const string name = "")
{
	DIR *dir = opendir((infold + '/' + name).c_str());
    if (dir != NULL)
	{
		// Create directory
		makeDir(outfold + '/' + name);

		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL)
		{
			if (string(ent->d_name).find("pregsc") != string::npos)
				continue;

			string x = !name.empty() ? name + '/' + ent->d_name : ent->d_name;
			switch (ent->d_type)
			{
				case DT_REG:
					struct stat info;
					stat((infold + '/' + x).c_str(), &info);
					if (ignoref.find(x) == ignoref.end() || ignoref[x] != info.st_mtime)
					{
						cout << "Processing file: " << x << "... ";
						string s = loadFile(infold + '/' + x);
						size_t i = 0;
						size_t n = s.size();
						depth = 0;
						parentIsGlobal = true;
						functions.clear();
						def = globaldef;
						par.clear();
						inherit.clear();
						//classVars.clear();
						/*#ifdef _WIN32
							_mkdir((outfold + '/' + name).c_str());
						#else
							mkdir((outfold + '/' + name).c_str(), 0777);
						#endif*/
						ofstream g((outfold + '/' + x).c_str());
						map<string, string> m;
						g << eval(0, m, s, i, n);

						if (!functions.empty())
						{
							writeFunction(g, "sif", "__sif(c,y,n){if(c)return y;else return n;}");
							writeFunction(g, "par", "__par(a,b){k=getArrayKeys(a);c=a.size;s=b+\" = array(\";for(i=0;i<c;i++){if(i)s+=\", \";s+=\"[\"+k[i]+\"] => \"+a[k[i]];}s+=\")\";iPrintLn(s);printLn(s)}");
						}

						if (!ext.empty())
						{
							string noext = x.substr(0, x.rfind('.'));
							s = noext;
							for (i = s.find('/'); i != string::npos; i = s.find('/', i + 1))
								s.replace(i, 1, "\\");

							g << endl << "#include " + outfold + '\\' + s + "_ext";
							for (auto z : ext)
							{
								ofstream w((outfold + '/' + noext + "_ext." + z.first).c_str());
								w << z.second;
								w.close();
							}

							ext.clear();
						}

						g.close();
						ignoref[x] = (size_t)info.st_mtime;
						cout << "Saved!" << endl;
					}
				break;
				case DT_DIR:
					if (string(ent->d_name) != "." && string(ent->d_name) != "..")
						listFiles(x);
				break;
			}
		}
        closedir(dir);
		return true;
    }
	else
	{
		return false;
    }
}

int main(int argc, char* argv[])
{
	//clock_t t = clock();
	cout << "/ ========================= /" << endl;
	cout << "/ GSC preprocessor by iCore /" << endl;
	cout << "/  moddb.com/members/icore  /" << endl;
	cout << "/       sharpkode.com       /" << endl;
	cout << "/ ========================= /" << endl << endl;
	try
	{
		// Environment dependent
		setlocale(LC_ALL, "");
	
		// Get params
		for (int j = 1; j < argc; ++j)
		{
			string a = argv[j]; // argv[j] must be converted to string
			if (a == "-infold")
			{
				if (argc > j + 1)
					infold = argv[++j];
				else
					warning("Parameter 'infold' requires a folder name");
			}
			else if (a == "-outfold")
			{
				if (argc > j + 1)
					outfold = argv[++j];
				else
					warning("Parameter 'outfold' requires a folder name");
			}
			else if (a == "-globaldef")
			{
				if (argc > j + 1)
					globaldef.insert(argv[++j]);
				else
					warning("Parameter 'globaldef' requires a name");
			}
			else if (a == "-d")
				debug = true;
			else if (a == "-nopause")
				ispause = false;
			else if (a == "-zeroarrays")
				zeroarrays = 1;
			else if (a == "-noforeach")
				zeroarrays = -1;
		}
	
		cout << "Loading pregsc.dat... ";

		// Ignore already processed files
		ifstream f("pregsc.dat");
		if (f.is_open())
		{
			while (!f.eof())
			{
				string t;
				getline(f, t);
				if (!t.empty())
				{
					string s2;
					getline(f, s2);
					ignoref[t] = atoi(s2.c_str());
				}
			}
			f.close();
		}
	
		cout << "Done!" << endl;
		
		// Process files
		makeDir(outfold);
		if (listFiles())
		{
			cout << "Saving pregsc.dat... ";

			// Save changes
			ofstream g("pregsc.dat");
			for(map<string, size_t>::const_iterator it = ignoref.begin(); it != ignoref.end(); ++it)
			{
				g << it->first << endl << it->second << endl;
			}
			g.close();
	
			cout << "Done!";
		}
		else
		{
			warning("'" + infold + "' folder not found in your mod directory!");
		}

		// End
		cout << endl << endl << "Success.";
	}
	catch (exception &e)
	{
		errorColor();
		cout << endl << endl << "FATAL ERROR: " << e.what();
		errorColor(false);
	}

	//cout << endl << (float)(clock() - t) / CLOCKS_PER_SEC << "s";

	// Pause
	if (ispause)
		pause();
}
