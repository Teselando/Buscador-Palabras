#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <sstream>
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <climits>
#include <functional>

namespace fs = std::filesystem;
using namespace std;

// --- ESTRUCTURAS Y CONFIGURACIÓN ---

enum ElementType { ANY, VOWEL, CONSONANT, EXACT };

struct PatternElement {
	ElementType type;
	int min_count;
	int max_count;
	char exact_char;
};

struct ResourceCondition {
	string op;
	int num;
	string target;
};

int memo_buffer[100][50][11];

// --- UTILIDADES DE TEXTO ---

string normalizeWord(const string& w) {
	string res;
	res.reserve(w.size());
	for (size_t i = 0; i < w.size(); ) {
		unsigned char c = w[i];
		if (c < 128) {
			if (c >= 'a' && c <= 'z') res += (char)toupper(c);
			else if (c >= 'A' && c <= 'Z') res += c;
			i++; continue;
		}
		if ((unsigned char)c == 0xC3 && i + 1 < w.size()) {
			unsigned char d = w[i + 1];
			switch (d) {
			case 0x81: case 0xA1: res += 'A'; break;
			case 0x89: case 0xA9: res += 'E'; break;
			case 0x8D: case 0xAD: res += 'I'; break;
			case 0x93: case 0xB3: res += 'O'; break;
			case 0x9A: case 0xBA: res += 'U'; break;
			case 0x9C: case 0xBC: res += 'U'; break;
			case 0x91: case 0xB1: res += '~'; break;
			}
			i += 2; continue;
		}
		i++;
	}
	return res;
}

inline bool isVowel(char c) {
	return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

inline bool isConsonant(char c) {
	if (c == '~') return true;
	return isalpha((unsigned char)c) && !isVowel(c);
}

vector<string> split(const string& s, char delim) {
	vector<string> result;
	stringstream ss(s);
	string item;
	while (getline(ss, item, delim)) result.push_back(item);
	return result;
}

// Conversión segura de string a int (evita excepciones)
int safeStoi(const string& s, int def = 0) {
	if (s.empty()) return def;
	try { return stoi(s); }
	catch (...) { return def; }
}

// Parsea una lista de condiciones separadas por comas (ej: ">=2V*,0K,3S*")
// y devuelve el vector de ResourceCondition equivalente.
vector<ResourceCondition> parseConditionList(const string& condStr) {
	vector<ResourceCondition> result;
	if (condStr.empty()) return result;
	for (string cond : split(condStr, ',')) {
		cond.erase(0, cond.find_first_not_of(" "));
		if (cond.empty()) continue;
		string op = "";
		int idx = 0;
		while (idx < (int)cond.size() && string("<>=").find(cond[idx]) != string::npos)
			op += cond[idx++];
		string num_str = "";
		while (idx < (int)cond.size() && isdigit((unsigned char)cond[idx]))
			num_str += cond[idx++];
		string target = cond.substr(idx);
		target.erase(0, target.find_first_not_of(" "));
		for (auto& c : target) c = (char)toupper((unsigned char)c);

		int num = 1;
		if (op.empty() && num_str.empty()) { op = ">="; num = 1; }
		else if (op.empty()) { op = "=="; num = safeStoi(num_str); }
		else if (!num_str.empty()) num = safeStoi(num_str);
		result.push_back({ op, num, target });
	}
	return result;
}

// Dado un string "rest" de un comando (tras quitar el prefijo y el espacio),
// extrae la palabra, las restricciones y la tolerancia.
// Formatos: "WORD [RESTR] TOL", "WORD TOL", "WORD"
tuple<string, string, string> extractWordRestrTol(const string& rest) {
	string word, extra_r = "", tol_str = "";
	size_t bs = rest.find('['), ls = rest.find_last_of(" ");
	if (bs != string::npos) {
		size_t be = rest.find(']', bs);
		if (be != string::npos) {
			word = rest.substr(0, bs);
			extra_r = rest.substr(bs + 1, be - bs - 1);
			tol_str = rest.substr(be + 1);
			tol_str.erase(0, tol_str.find_first_not_of(" "));
		}
	}
	else if (ls != string::npos) {
		string pn = rest.substr(ls + 1);
		string cn = pn;
		if (!cn.empty() && cn.back() == '*') cn.pop_back();
		if (!cn.empty() && all_of(cn.begin(), cn.end(), [](unsigned char c) { return isdigit(c); })) {
			word = rest.substr(0, ls);
			tol_str = pn;
		}
		else word = rest;
	}
	else word = rest;
	word.erase(remove(word.begin(), word.end(), ' '), word.end());
	return { word, extra_r, tol_str };
}

// Determina si dos consonantes forman un inicio de sílaba válido en español
bool isValidOnset(char c1, char c2) {
	c1 = (char)toupper(c1); c2 = (char)toupper(c2);
	if (c1 == 'C' && c2 == 'H') return true;
	if (c1 == 'L' && c2 == 'L') return true;
	if (c1 == 'R' && c2 == 'R') return true;
	string valid = "PR BR TR DR CR GR FR PL BL CL GL FL";
	string pair = { c1, c2 };
	return valid.find(pair) != string::npos;
}

// Separa una palabra normalizada en sílabas
vector<string> getSyllables(const string& word) {
	vector<pair<int, int>> nuclei;
	int n = word.length();
	int i = 0;

	// 1. Identificar núcleos vocálicos (incluyendo diptongos e ignorando hiatos)
	while (i < n) {
		if (isVowel(word[i])) {
			int start = i;
			int end = i;
			while (end + 1 < n && isVowel(word[end + 1])) {
				char prev = word[end];
				char curr = word[end + 1];
				bool prev_strong = (prev == 'A' || prev == 'E' || prev == 'O');
				bool curr_strong = (curr == 'A' || curr == 'E' || curr == 'O');
				if (prev_strong && curr_strong) break; // Hiato detectado (ej. A-E)
				end++;
			}
			nuclei.push_back({ start, end });
			i = end + 1;
		}
		else {
			i++;
		}
	}

	if (nuclei.empty()) return { word };

	// 2. Determinar los puntos de corte entre consonantes
	vector<int> split_points;
	for (size_t k = 0; k + 1 < nuclei.size(); ++k) {
		int c_start = nuclei[k].second + 1;
		int c_end = nuclei[k + 1].first - 1;
		int L = c_end - c_start + 1;

		if (L <= 0) split_points.push_back(c_start);
		else if (L == 1) split_points.push_back(c_start);
		else if (L == 2) {
			if (isValidOnset(word[c_start], word[c_start + 1])) split_points.push_back(c_start);
			else split_points.push_back(c_start + 1);
		}
		else if (L == 3) {
			if (isValidOnset(word[c_start + 1], word[c_start + 2])) split_points.push_back(c_start + 1);
			else split_points.push_back(c_start + 2);
		}
		else {
			split_points.push_back(c_start + 2);
		}
	}

	// 3. Cortar la palabra
	vector<string> syllables;
	int current_start = 0;
	for (int sp : split_points) {
		syllables.push_back(word.substr(current_start, sp - current_start));
		current_start = sp;
	}
	syllables.push_back(word.substr(current_start));
	return syllables;
}

// Devuelve la posición de la sílaba tónica contando desde la derecha (1=aguda, 2=llana, 3=esdrújula...)
int getStressPosition(const string& raw) {
	string norm;
	int accent_norm_pos = -1;

	for (size_t i = 0; i < raw.size(); ) {
		unsigned char c = raw[i];
		if (c < 128) {
			if (c >= 'a' && c <= 'z') { norm += (char)toupper(c); i++; }
			else if (c >= 'A' && c <= 'Z') { norm += c; i++; }
			else i++;
		}
		else if (c == 0xC3 && i + 1 < raw.size()) {
			unsigned char d = raw[i + 1];
			char mapped = 0; bool accented = false;
			switch (d) {
			case 0x81: case 0xA1: mapped = 'A'; accented = true; break;
			case 0x89: case 0xA9: mapped = 'E'; accented = true; break;
			case 0x8D: case 0xAD: mapped = 'I'; accented = true; break;
			case 0x93: case 0xB3: mapped = 'O'; accented = true; break;
			case 0x9A: case 0xBA: mapped = 'U'; accented = true; break;
			case 0x9C: case 0xBC: mapped = 'U'; break;
			case 0x91: case 0xB1: mapped = '~'; break;
			}
			if (mapped) { if (accented) accent_norm_pos = (int)norm.size(); norm += mapped; }
			i += 2;
		}
		else i++;
	}

	if (norm.empty()) return 2;
	vector<string> syllables = getSyllables(norm);
	int n_syl = (int)syllables.size();

	if (accent_norm_pos >= 0) {
		int pos = 0;
		for (int s = 0; s < n_syl; s++) {
			int end_pos = pos + (int)syllables[s].size();
			if (accent_norm_pos >= pos && accent_norm_pos < end_pos)
				return n_syl - s;
			pos = end_pos;
		}
	}

	// Reglas ortográficas por defecto
	char last = norm.back();
	if (isVowel(last) || last == 'N' || last == 'S') return 2; // llana
	return 1; // aguda
}

// Devuelve el sufijo de rima (desde la vocal tónica) en forma normalizada
string getRhymeSuffix(const string& raw) {
	string norm = normalizeWord(raw);
	if (norm.empty()) return "";
	vector<string> syllables = getSyllables(norm);
	int n_syl = (int)syllables.size();
	int stress_pos = getStressPosition(raw); // 1=aguda, 2=llana...
	int stress_idx = n_syl - stress_pos;
	if (stress_idx < 0) stress_idx = 0;
	if (stress_idx >= n_syl) stress_idx = n_syl - 1;

	// Concatenar desde la sílaba tónica
	string suffix = "";
	for (int i = stress_idx; i < n_syl; i++) suffix += syllables[i];

	// Recortar hasta la primera vocal
	for (size_t i = 0; i < suffix.size(); i++) {
		if (isVowel(suffix[i])) return suffix.substr(i);
	}
	return suffix;
}


int levenshtein(const string& a, const string& b, int max_d = INT_MAX) {
	int m = (int)a.size(), n = (int)b.size();
	if (abs(m - n) > max_d) return abs(m - n);
	vector<int> prev(n + 1), curr(n + 1);
	for (int j = 0; j <= n; j++) prev[j] = j;
	for (int i = 1; i <= m; i++) {
		curr[0] = i;
		int row_min = curr[0];
		for (int j = 1; j <= n; j++) {
			int sub = prev[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0);
			int del = prev[j] + 1;
			int ins = curr[j - 1] + 1;
			curr[j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
			if (curr[j] < row_min) row_min = curr[j];
		}
		if (row_min > max_d) return max_d + 1;
		swap(prev, curr);
	}
	return prev[n];
}

// --- GESTIÓN DE ARCHIVOS Y CACHÉ ---

void saveBinaryCache(const string& filename, const vector<string>& raw, const vector<string>& norm) {
	ofstream out(filename, ios::binary);
	size_t size = raw.size();
	out.write((char*)&size, sizeof(size));
	for (size_t i = 0; i < size; ++i) {
		size_t r_len = raw[i].size(), n_len = norm[i].size();
		out.write((char*)&r_len, sizeof(r_len));
		out.write(raw[i].c_str(), r_len);
		out.write((char*)&n_len, sizeof(n_len));
		out.write(norm[i].c_str(), n_len);
	}
}

bool loadBinaryCache(const string& filename, vector<string>& raw, vector<string>& norm) {
	ifstream in(filename, ios::binary);
	if (!in) return false;
	size_t size;
	in.read((char*)&size, sizeof(size));
	raw.resize(size); norm.resize(size);
	for (size_t i = 0; i < size; ++i) {
		size_t r_len, n_len;
		in.read((char*)&r_len, sizeof(r_len)); raw[i].resize(r_len);
		in.read(&raw[i][0], r_len);
		in.read((char*)&n_len, sizeof(n_len)); norm[i].resize(n_len);
		in.read(&norm[i][0], n_len);
	}
	return true;
}

void listDictionaries() {
	cout << "\nDiccionarios disponibles (.txt):" << endl;
	bool found = false;
	for (const auto& entry : fs::directory_iterator(".")) {
		if (entry.path().extension() == ".txt") {
			cout << " - " << entry.path().stem().string() << endl;
			found = true;
		}
	}
	if (!found) cout << " (No se encontraron archivos .txt)" << endl;
}

bool loadDictionary(string name, vector<string>& raw, vector<string>& norm) {
	string txtFile = name + ".txt";
	string binFile = name + ".bin";
	raw.clear(); norm.clear();

	cout << "Cargando '" << name << "'... ";
	if (!loadBinaryCache(binFile, raw, norm)) {
		ifstream file(txtFile);
		if (!file) { cout << "\nError: no se encontró el archivo '" << txtFile << "'\n"; return false; }
		string line;
		while (getline(file, line)) {
			if (!line.empty()) {
				raw.push_back(line);
				norm.push_back(normalizeWord(line));
			}
		}
		saveBinaryCache(binFile, raw, norm);
	}
	cout << "[OK] " << norm.size() << " palabras cargadas.\n";
	return true;
}

// --- LÓGICA DE BÚSQUEDA ---

void parseInput(string input, vector<PatternElement>& elems, vector<ResourceCondition>& resources, int& tolerance, bool& is_total, bool* parse_error = nullptr) {
	input.erase(0, input.find_first_not_of(" "));
	input.erase(input.find_last_not_of(" ") + 1);

	string p_str = input, r_str = "";
	tolerance = 0;
	is_total = false;

	size_t b_start = input.find('[');
	if (b_start != string::npos) {
		size_t b_end = input.find(']', b_start);
		if (b_end != string::npos) {
			p_str = input.substr(0, b_start);
			r_str = input.substr(b_start + 1, b_end - b_start - 1);
			string rem = input.substr(b_end + 1);
			rem.erase(0, rem.find_first_not_of(" "));
			if (!rem.empty()) {
				if (rem.back() == '*') { is_total = true; rem.pop_back(); }
				if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char ch) { return isdigit(ch); }))
					tolerance = safeStoi(rem);
				else is_total = false;
			}
		}
	}
	else {
		size_t last_space = input.find_last_of(" ");
		if (last_space != string::npos) {
			string rem = input.substr(last_space + 1);
			if (!rem.empty()) {
				bool local_is_total = false;
				if (rem.back() == '*') { local_is_total = true; rem.pop_back(); }
				if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char ch) { return isdigit(ch); })) {
					tolerance = safeStoi(rem);
					is_total = local_is_total;
					p_str = input.substr(0, last_space);
				}
			}
		}
	}

	for (size_t i = 0; i < p_str.length(); ++i) {
		char c = p_str[i];
		if (c == '*') elems.push_back({ ANY, 1, 1, 0 });
		else if (c == '.') elems.push_back({ ANY, 0, 99, 0 });
		else if (c == '(') {
			size_t j = p_str.find(')', i);
			if (j == string::npos) break;
			string inside = p_str.substr(i + 1, j - i - 1);
			auto parts = split(inside, ',');
			// Validate: a range spec has up to 3 parts: digits, digits, V|C
			bool valid_range = (parts.size() <= 3);
			if (valid_range && !parts.empty() && !parts[0].empty())
				valid_range = all_of(parts[0].begin(), parts[0].end(), [](unsigned char ch) { return isdigit(ch); });
			if (valid_range && parts.size() >= 2 && !parts[1].empty())
				valid_range = all_of(parts[1].begin(), parts[1].end(), [](unsigned char ch) { return isdigit(ch); });
			if (valid_range && parts.size() >= 3 && !parts[2].empty())
				valid_range = (parts[2] == "V" || parts[2] == "C");
			if (!valid_range) {
				if (parse_error) *parse_error = true;
				elems.clear();
				break;
			}
			int min_c = (parts.size() > 0 && !parts[0].empty()) ? safeStoi(parts[0]) : 0;
			int max_c = (parts.size() > 1 && !parts[1].empty()) ? safeStoi(parts[1]) : 99;
			ElementType type = ANY;
			if (parts.size() > 2 && !parts[2].empty()) {
				if (parts[2] == "V") type = VOWEL;
				else if (parts[2] == "C") type = CONSONANT;
			}
			elems.push_back({ type, min_c, max_c, 0 });
			i = j;
		}
		else if (isalpha((unsigned char)c) || c == '~') {
			elems.push_back({ EXACT, 1, 1, (char)toupper((unsigned char)c) });
		}
	}

	if (!r_str.empty())
		resources = parseConditionList(r_str);
}

int checkResources(const string& word, const string& raw_word, const vector<ResourceCondition>& resources) {
	if (resources.empty()) return 0;
	int v_count = 0, c_count = 0, l_count[256] = { 0 };
	for (char c : word) {
		if (isVowel(c)) v_count++;
		if (isConsonant(c)) c_count++;
		l_count[(unsigned char)c]++;
	}

	int total_errors = 0;
	for (const auto& res : resources) {
		int val = 0;
		if (res.target == "V*") val = v_count;
		else if (res.target == "C*") val = c_count;
		else if (res.target == "S*") val = (int)getSyllables(word).size();
		else if (res.target == "T*") val = getStressPosition(raw_word);
		else if (res.target == "") val = (int)word.length();
		else if (res.target.size() == 1) val = l_count[(unsigned char)res.target[0]];
		else {
			// Subcadena: contar ocurrencias de res.target en word
			size_t pos = 0;
			while ((pos = word.find(res.target, pos)) != string::npos) { val++; pos++; }
		}

		if (res.op == "==") total_errors += abs(val - res.num);
		else if (res.op == ">=") total_errors += (val < res.num) ? res.num - val : 0;
		else if (res.op == "<=") total_errors += (val > res.num) ? val - res.num : 0;
		else if (res.op == ">") total_errors += (val <= res.num) ? (res.num + 1) - val : 0;
		else if (res.op == "<") total_errors += (val >= res.num) ? val - (res.num - 1) : 0;
	}
	return total_errors;
}

bool matchPattern(const string& word, int w_idx, const vector<PatternElement>& elems, int e_idx, int err_left) {
	if (err_left < 0) return false;
	if (e_idx == (int)elems.size()) return (int)word.length() - w_idx <= err_left;
	if (memo_buffer[w_idx][e_idx][err_left] != -1) return memo_buffer[w_idx][e_idx][err_left] == 1;

	const auto& E = elems[e_idx];
	bool matched = false;
	int max_l = (E.max_count > 50) ? (int)word.length() - w_idx : E.max_count + err_left;

	for (int L = 0; w_idx + L <= (int)word.length() && L <= max_l; ++L) {
		int c_err = 0;
		int m_chars = (std::min)(L, E.max_count);
		int extra = (L > E.max_count) ? L - E.max_count : 0;
		int miss = (L < E.min_count) ? E.min_count - L : 0;

		for (int i = 0; i < m_chars; ++i) {
			char c = word[w_idx + i];
			if (E.type == EXACT && c != E.exact_char) c_err++;
			else if (E.type == VOWEL && !isVowel(c)) c_err++;
			else if (E.type == CONSONANT && !isConsonant(c)) c_err++;
		}
		if (c_err + extra + miss <= err_left) {
			if (matchPattern(word, w_idx + L, elems, e_idx + 1, err_left - (c_err + extra + miss))) {
				matched = true; break;
			}
		}
	}
	return (memo_buffer[w_idx][e_idx][err_left] = matched ? 1 : 0);
}

// --- MOTOR DE CONSULTAS BOOLEANAS ---

// Devuelve true si s tiene operadores booleanos en el nivel 0 (fuera de () y [])
static bool hasBoolOps(const string& s) {
	int dp = 0, db = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '[') { db++; continue; }
		if (s[i] == ']') { if (db > 0) db--; continue; }
		if (db > 0) continue;
		if (s[i] == '(') { dp++; continue; }
		if (s[i] == ')') { if (dp > 0) dp--; continue; }
		if (dp > 0) continue;
		if (s[i] == '!') return true;
		if (i + 1 < s.size() && s[i] == '&' && s[i + 1] == '&') return true;
		if (i + 1 < s.size() && s[i] == '|' && s[i + 1] == '|') return true;
		if (s[i] == '-' && i > 0 && s[i - 1] == ' ' && i + 1 < s.size() && s[i + 1] == ' ') return true;
	}
	return false;
}

// Ejecuta una consulta hoja y devuelve un bitmask sobre el diccionario
static vector<bool> runLeafQuery(
	string input,
	const vector<string>& dictionary,
	const vector<string>& raw_dict,
	const unordered_map<string, string>& normToRaw,
	const map<int, vector<string>>& dictByLen,
	ostream* vout = nullptr
) {
	vector<bool> matched(dictionary.size(), false);
	input.erase(0, input.find_first_not_of(" \t\r\n"));
	{ size_t l = input.find_last_not_of(" \t\r\n"); if (l != string::npos) input.erase(l + 1); }
	for (char& c : input) if (c == '\\') c = '/';
	if (input.empty()) return matched;

	string inputLine = input;
	vector<string> patterns_to_run;

	// /rd: quitar el prefijo n, tratar el resto como consulta
	{
		bool isRd_ = (input.size() >= 7 && input.substr(0, 7) == "/random") ||
			(input.size() >= 3 && input.substr(0, 3) == "/rd" && (input.size() == 3 || input[3] == ' '));
		if (isRd_) {
			string rest = (input.substr(0, 7) == "/random") ? input.substr(7) : input.substr(3);
			rest.erase(0, rest.find_first_not_of(" "));
			size_t sp = rest.find(' ');
			string first = (sp != string::npos) ? rest.substr(0, sp) : rest;
			bool fn = !first.empty() && all_of(first.begin(), first.end(), [](unsigned char c) { return isdigit(c); });
			inputLine = fn ? rest.substr(sp + 1) : rest;
			if (inputLine.empty()) inputLine = ".";
			// Si la línea empieza directamente por '[', no hay patrón: añadir '.'
			if (!inputLine.empty() && inputLine[0] == '[') inputLine = ". " + inputLine;
			input = inputLine;
		}
	}

	// /cal: devuelve conjunto de palabras individuales de las divisiones
	{
		bool isCal_ = (input.size() >= 10 && input.substr(0, 10) == "/calembour") ||
			(input.size() >= 4 && input.substr(0, 4) == "/cal" && (input.size() == 4 || input[4] == ' '));
		if (isCal_) {
			string rest = (input.substr(0, 10) == "/calembour") ? input.substr(10) : input.substr(4);
			rest.erase(0, rest.find_first_not_of(" "));
			int cal_n = 0; string cal_word = rest, cal_restr = "";
			size_t bs = rest.find('[');
			if (bs != string::npos) {
				size_t be = rest.find(']', bs);
				if (be != string::npos) {
					cal_word = rest.substr(0, bs);
					cal_restr = rest.substr(bs + 1, be - bs - 1);
					string rem = rest.substr(be + 1); rem.erase(0, rem.find_first_not_of(" "));
					if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char c) { return isdigit(c); })) cal_n = safeStoi(rem);
				}
			}
			else {
				size_t lsp = rest.find_last_of(" ");
				if (lsp != string::npos) {
					string pn = rest.substr(lsp + 1);
					if (!pn.empty() && all_of(pn.begin(), pn.end(), [](unsigned char c) { return isdigit(c); })) { cal_n = safeStoi(pn); cal_word = rest.substr(0, lsp); }
				}
			}
			cal_word.erase(remove(cal_word.begin(), cal_word.end(), ' '), cal_word.end());
			string normCal = normalizeWord(cal_word);
			if (normCal.empty() || (int)normCal.size() > 20) return matched;

			vector<ResourceCondition> cal_res = parseConditionList(cal_restr);

			int L = (int)normCal.size();
			vector<vector<pair<int, string>>> best(L, vector<pair<int, string>>(L + 1, make_pair(INT_MAX, string(""))));
			for (int i = 0; i < L; i++) for (int j = i + 1; j <= L; j++) {
				string part = normCal.substr(i, j - i); int plen = (int)part.size();
				auto it = normToRaw.find(part);
				if (it != normToRaw.end()) {
					if (cal_res.empty() || checkResources(part, it->second, cal_res) == 0) best[i][j] = make_pair(0, it->second);
					continue;
				}
				if (cal_n == 0) continue;
				int be = cal_n + 1; string bw = "";
				for (int len = (std::max)(1, plen - cal_n); len <= plen + cal_n; len++) {
					auto il = dictByLen.find(len); if (il == dictByLen.end()) continue;
					for (const string& w : il->second) {
						if (!cal_res.empty() && checkResources(w, normToRaw.at(w), cal_res) > 0) continue;
						int d = levenshtein(part, w, be - 1);
						if (d < be) { be = d; bw = normToRaw.at(w); if (be == 0) break; }
					}
					if (be == 0) break;
				}
				if (be <= cal_n) best[i][j] = make_pair(be, bw);
			}

			// Construir mapa norma→índice
			unordered_map<string, size_t> normToIdx;
			for (size_t i = 0; i < dictionary.size(); i++)
				if (!normToIdx.count(dictionary[i])) normToIdx[dictionary[i]] = i;

			// Colectar todas las divisiones válidas y marcar palabras
			vector<vector<pair<string, int>>> cal_all;
			std::function<void(int, int, vector<pair<string, int>>&)> cs =
				[&](int pos, int err_left, vector<pair<string, int>>& cur) {
				if (pos == L) { if ((int)cur.size() >= 2) cal_all.push_back(cur); return; }
				for (int end = pos + 1; end <= L; end++) {
					int berr = best[pos][end].first;
					if (berr == INT_MAX || berr > err_left) continue;
					cur.push_back(make_pair(best[pos][end].second, berr));
					cs(end, err_left - berr, cur);
					cur.pop_back();
				}
				};
			vector<pair<string, int>> curt; cs(0, cal_n, curt);

			for (size_t si = 0; si < cal_all.size(); si++)
				for (size_t sj = 0; sj < cal_all[si].size(); sj++) {
					string n2 = normalizeWord(cal_all[si][sj].first);
					auto it2 = normToIdx.find(n2);
					if (it2 != normToIdx.end()) matched[it2->second] = true;
				}
			return matched;
		}
	}

	// /ang y /par
	{
		bool isAn_ = (input.size() >= 8 && input.substr(0, 8) == "/anagram") ||
			(input.size() >= 4 && input.substr(0, 4) == "/ang" && (input.size() == 4 || input[4] == ' '));
		bool isPar_ = (input.size() >= 12 && input.substr(0, 12) == "/paronomasia") ||
			(input.size() >= 4 && input.substr(0, 4) == "/par" && (input.size() == 4 || input[4] == ' '));
		if (isAn_ || isPar_) {
			string rest;
			if (isAn_) rest = (input.size() >= 8 && input.substr(0, 8) == "/anagram") ? input.substr(8) : input.substr(4);
			else       rest = (input.size() >= 12 && input.substr(0, 12) == "/paronomasia") ? input.substr(12) : input.substr(4);
			rest.erase(0, rest.find_first_not_of(" "));

			auto [word, extra_r, tol_str] = extractWordRestrTol(rest);
			string nw = normalizeWord(word);
			if (isAn_) {
				if (!tol_str.empty()) { string tmp = tol_str; if (tmp.back() == '*') tmp.pop_back(); if (all_of(tmp.begin(), tmp.end(), [](unsigned char c) {return isdigit(c);})) tol_str = tmp + "*"; }
				map<char, int> cnt; for (char c : nw) cnt[c]++;
				string exp = ". [";
				for (auto it = cnt.begin(); it != cnt.end(); ++it) exp += to_string(it->second) + it->first + ",";
				exp += to_string(nw.length());
				if (!extra_r.empty()) exp += "," + extra_r;
				exp += "] " + tol_str;
				inputLine = exp;
			}
			else {
				// Construir el esqueleto consonántico: las consonantes quedan en su
				// posición y cada vocal (o grupo de vocales) se sustituye por *.
				// Ejemplo: COCHE -> C*CH* [2V*]
				string pat = ""; int vow = 0; string cgrp = "";
				for (char c : nw) {
					if (isVowel(c)) { pat += cgrp + "*"; cgrp = ""; vow++; }
					else cgrp += c;
				}
				pat += cgrp; // consonantes finales sin vocal siguiente
				string exp = pat + " [" + to_string(vow) + "V*";
				if (!extra_r.empty()) exp += "," + extra_r;
				exp += "] " + tol_str;
				inputLine = exp;
			}
		}
	}

	// /ans
	{
		bool isAns_ = (input.size() >= 12 && input.substr(0, 12) == "/anasyllabic") ||
			(input.size() >= 4 && input.substr(0, 4) == "/ans" && (input.size() == 4 || input[4] == ' '));
		if (isAns_) {
			string rest = (input.size() >= 12 && input.substr(0, 12) == "/anasyllabic") ? input.substr(12) : input.substr(4);
			rest.erase(0, rest.find_first_not_of(" "));
			auto [word, extra_r, tol_str] = extractWordRestrTol(rest);
			string nw = normalizeWord(word);
			vector<string> syl = getSyllables(nw); sort(syl.begin(), syl.end());
			do {
				string p = ""; for (const string& s2 : syl) p += s2;
				if (!extra_r.empty()) p += " [" + extra_r + "]";
				if (!tol_str.empty()) p += " " + tol_str;
				patterns_to_run.push_back(p);
			} while (next_permutation(syl.begin(), syl.end()));
		}
	}

	// /aso y /con (rima asonante / consonante)
	{
		bool isAso_ = (input.size() >= 9 && input.substr(0, 9) == "/assonant") ||
			(input.size() >= 4 && input.substr(0, 4) == "/aso" && (input.size() == 4 || input[4] == ' '));
		bool isCon_ = (input.size() >= 10 && input.substr(0, 10) == "/consonant") ||
			(input.size() >= 4 && input.substr(0, 4) == "/con" && (input.size() == 4 || input[4] == ' '));
		if (isAso_ || isCon_) {
			int plen;
			if (isAso_) plen = (input.size() >= 9 && input.substr(0, 9) == "/assonant") ? 9 : 4;
			else        plen = (input.size() >= 10 && input.substr(0, 10) == "/consonant") ? 10 : 4;

			string rest = input.substr(plen);
			rest.erase(0, rest.find_first_not_of(" "));
			auto [word, extra_r, tol_str] = extractWordRestrTol(rest);
			string rhyme = getRhymeSuffix(word);

			if (!rhyme.empty()) {
				if (isCon_) {
					// Rima consonante: mismo sufijo exacto desde vocal tónica
					inputLine = "." + rhyme;
				}
				else {
					// Rima asonante: solo las vocales del sufijo
					string pat = ".(0,,C)";
					for (char c : rhyme)
						if (isVowel(c)) pat += string(1, c) + "(0,,C)";
					inputLine = pat;
				}
				if (!extra_r.empty()) inputLine += " [" + extra_r + "]";
				if (!tol_str.empty()) inputLine += " " + tol_str;
			}
		}
	}

	// /anp /epi /mul /uni
	{
		bool isAnp_ = (input.size() >= 9 && input.substr(0, 9) == "/anaphora") ||
			(input.size() >= 4 && input.substr(0, 4) == "/anp" && (input.size() == 4 || input[4] == ' '));
		bool isEpi_ = (input.size() >= 9 && input.substr(0, 9) == "/epiphora") ||
			(input.size() >= 4 && input.substr(0, 4) == "/epi" && (input.size() == 4 || input[4] == ' '));
		bool isMul_ = (input.size() >= 14 && input.substr(0, 14) == "/multisyllabic") ||
			(input.size() >= 4 && input.substr(0, 4) == "/mul" && (input.size() == 4 || input[4] == ' '));
		bool isUni_ = (input.size() >= 12 && input.substr(0, 12) == "/univocalism") ||
			(input.size() >= 4 && input.substr(0, 4) == "/uni" && (input.size() == 4 || input[4] == ' '));
		if (isAnp_ || isEpi_ || isMul_ || isUni_) {
			string rest;
			if (isAnp_)      rest = (input.size() >= 9 && input.substr(0, 9) == "/anaphora") ? input.substr(9) : input.substr(4);
			else if (isEpi_) rest = (input.size() >= 9 && input.substr(0, 9) == "/epiphora") ? input.substr(9) : input.substr(4);
			else if (isMul_) rest = (input.size() >= 14 && input.substr(0, 14) == "/multisyllabic") ? input.substr(14) : input.substr(4);
			else             rest = (input.size() >= 12 && input.substr(0, 12) == "/univocalism") ? input.substr(12) : input.substr(4);
			rest.erase(0, rest.find_first_not_of(" "));
			auto [word, extra_r, tol_str] = extractWordRestrTol(rest);
			string nw = normalizeWord(word);
			if (isAnp_) {
				inputLine = nw + ".";
				if (!extra_r.empty()) inputLine += " [" + extra_r + "]";
				if (!tol_str.empty()) inputLine += " " + tol_str;
			}
			else if (isEpi_) {
				inputLine = "." + nw;
				if (!extra_r.empty()) inputLine += " [" + extra_r + "]";
				if (!tol_str.empty()) inputLine += " " + tol_str;
			}
			else if (isMul_) {
				string pat = "."; int vow = 0;
				for (char c : nw) if (isVowel(c)) { pat += c; pat += "."; vow++; }
				inputLine = pat + " [" + to_string(vow) + "V*";
				if (!extra_r.empty()) inputLine += "," + extra_r;
				inputLine += "]";
				if (!tol_str.empty()) inputLine += " " + tol_str;
			}
			else if (isUni_) {
				char v = '\0'; for (char c : nw) if (isVowel(c)) { v = c; break; }
				if (v) {
					inputLine = ". [" + string(1, v);
					string av = "AEIOU"; for (char c : av) if (c != v) inputLine += ",0" + string(1, c);
					if (!extra_r.empty()) inputLine += "," + extra_r;
					inputLine += "]";
					if (!tol_str.empty()) inputLine += " " + tol_str;
				}
			}
		}
	}

	// /wp
	bool isWp_ = (input.size() >= 9 && input.substr(0, 9) == "/wordplay") ||
		(input.size() >= 3 && input.substr(0, 3) == "/wp" && (input.size() == 3 || input[3] == ' '));
	string wp_word_ = "", wp_restr_ = "";
	int wp_n_ = 1; bool wp_ast_ = false;
	if (isWp_) {
		string rest = (input.substr(0, 9) == "/wordplay") ? input.substr(9) : input.substr(3);
		rest.erase(0, rest.find_first_not_of(" "));
		size_t b2 = rest.find('['); size_t ls = rest.find_last_of(" ");
		if (b2 != string::npos) {
			wp_word_ = rest.substr(0, b2);
			size_t be = rest.find(']', b2);
			if (be != string::npos) {
				wp_restr_ = rest.substr(b2 + 1, be - b2 - 1);
				string pn = rest.substr(be + 1); pn.erase(0, pn.find_first_not_of(" "));
				if (!pn.empty()) { if (pn.back() == '*') { wp_ast_ = true; pn.pop_back(); } if (!pn.empty() && all_of(pn.begin(), pn.end(), [](unsigned char c) {return isdigit(c);})) wp_n_ = safeStoi(pn); }
			}
		}
		else if (ls != string::npos) {
			string pn = rest.substr(ls + 1); string cn = pn; if (!cn.empty() && cn.back() == '*') { wp_ast_ = true; cn.pop_back(); }
			if (!cn.empty() && all_of(cn.begin(), cn.end(), [](unsigned char c) { return isdigit(c); })) { wp_word_ = rest.substr(0, ls); wp_n_ = safeStoi(cn); }
			else wp_word_ = rest;
		}
		else wp_word_ = rest;
		wp_word_.erase(remove(wp_word_.begin(), wp_word_.end(), ' '), wp_word_.end());
	}

	// Bucle de búsqueda
	bool isAns_loop = (input.size() >= 12 && input.substr(0, 12) == "/anasyllabic") ||
		(input.size() >= 4 && input.substr(0, 4) == "/ans" && (input.size() == 4 || input[4] == ' '));
	while (true) {
		if (isWp_) {
			inputLine = wp_word_;
			if (!wp_restr_.empty()) inputLine += " [" + wp_restr_ + "]";
			inputLine += " " + to_string(wp_n_) + (wp_ast_ ? "*" : "");
			patterns_to_run = { inputLine };
		}
		else if (!isAns_loop) {
			patterns_to_run = { inputLine };
		}
		fill(matched.begin(), matched.end(), false);
		for (const string& pLine : patterns_to_run) {
			vector<PatternElement> elems; vector<ResourceCondition> resources;
			int tolerance = 0; bool is_total = false;
			parseInput(pLine, elems, resources, tolerance, is_total);
			for (size_t i = 0; i < dictionary.size(); i++) {
				if (matched[i]) continue;
				const string& w = dictionary[i];
				if (w.length() >= 100) continue;
				int res_errors = checkResources(w, raw_dict[i], resources);
				int rem_tol = tolerance;
				if (is_total) { if (res_errors > tolerance) continue; rem_tol -= res_errors; }
				else { if (res_errors > 0) continue; }
				for (int r = 0; r <= (int)w.length(); r++)
					for (int e2 = 0; e2 <= (int)elems.size(); e2++)
						for (int t = 0; t <= rem_tol; t++) memo_buffer[r][e2][t] = -1;
				if (matchPattern(w, 0, elems, 0, rem_tol)) matched[i] = true;
			}
		}
		if (isWp_) {
			int cnt = 0; for (bool b : matched) if (b) cnt++;
			bool self_only = false;
			if (cnt == 1) {
				for (size_t i = 0; i < dictionary.size(); i++)
					if (matched[i] && normalizeWord(raw_dict[i]) == normalizeWord(wp_word_)) { self_only = true; break; }
			}
			if ((cnt == 0 || self_only) && wp_n_ < 99) { wp_n_++; continue; }
			if (vout) *vout << "(B\xC3\xBAsqueda completada con n = " << wp_n_ << ")\n";
		}
		break;
	}
	return matched;
}

// --- EXPRESIONES BOOLEANAS ---

struct BoolExpr {
	enum Op { LEAF, AND_OP, OR_OP, NOT_OP, DIFF_OP } op;
	string query;
	vector<BoolExpr> children;
	BoolExpr() : op(LEAF) {}
};

enum BoolTokT { BT_AND, BT_OR, BT_DIFF, BT_NOT, BT_ATOM };
struct BoolToken { BoolTokT type; string content; };

static vector<BoolToken> tokenizeBool(const string& s) {
	vector<BoolToken> toks;
	size_t i = 0, n = s.size();
	while (i < n) {
		if (s[i] == ' ' || s[i] == '\t') { i++; continue; }
		if (s[i] == '!') { toks.push_back({ BT_NOT, "" }); i++; continue; }
		if (i + 1 < n && s[i] == '&' && s[i + 1] == '&') { toks.push_back({ BT_AND, "" }); i += 2; continue; }
		if (i + 1 < n && s[i] == '|' && s[i + 1] == '|') { toks.push_back({ BT_OR, "" }); i += 2; continue; }
		if (s[i] == '-' && i > 0 && s[i - 1] == ' ' && i + 1 < n && s[i + 1] == ' ') { toks.push_back({ BT_DIFF, "" }); i++; continue; }
		if (s[i] == '(') {
			int dp = 0, db = 0; size_t start = i;
			while (i < n) {
				if (s[i] == '[') db++;
				else if (s[i] == ']' && db > 0) db--;
				else if (s[i] == '(' && db == 0) dp++;
				else if (s[i] == ')' && db == 0) { dp--; if (dp == 0) { i++; break; } }
				i++;
			}
			string content = s.substr(start + 1, i - start - 2);
			for (char& c : content) if (c == '\\') c = '/';
			toks.push_back({ BT_ATOM, content });
			continue;
		}
		i++;
	}
	return toks;
}

struct BoolParser {
	vector<BoolToken> toks;
	size_t pos;
	BoolParser(const vector<BoolToken>& t) : toks(t), pos(0) {}
	bool atEnd() const { return pos >= toks.size(); }
	BoolTokT peekType() const { return pos < toks.size() ? toks[pos].type : BT_ATOM; }
	BoolToken consume() { return toks[pos++]; }

	BoolExpr parseExpr() {
		BoolExpr left = parseDiff();
		while (!atEnd() && peekType() == BT_OR) {
			consume();
			BoolExpr right = parseDiff();
			BoolExpr node; node.op = BoolExpr::OR_OP;
			node.children.push_back(left); node.children.push_back(right);
			left = node;
		}
		return left;
	}
	BoolExpr parseDiff() {
		BoolExpr left = parseConj();
		while (!atEnd() && peekType() == BT_DIFF) {
			consume();
			BoolExpr right = parseConj();
			BoolExpr node; node.op = BoolExpr::DIFF_OP;
			node.children.push_back(left); node.children.push_back(right);
			left = node;
		}
		return left;
	}
	BoolExpr parseConj() {
		BoolExpr left = parseUnary();
		while (!atEnd() && peekType() == BT_AND) {
			consume();
			BoolExpr right = parseUnary();
			BoolExpr node; node.op = BoolExpr::AND_OP;
			node.children.push_back(left); node.children.push_back(right);
			left = node;
		}
		return left;
	}
	BoolExpr parseUnary() {
		if (!atEnd() && peekType() == BT_NOT) {
			consume();
			BoolExpr child = parseUnary();
			BoolExpr node; node.op = BoolExpr::NOT_OP;
			node.children.push_back(child);
			return node;
		}
		return parseAtom();
	}
	BoolExpr parseAtom() {
		if (!atEnd() && peekType() == BT_ATOM) {
			BoolToken tok = consume();
			if (hasBoolOps(tok.content)) {
				BoolParser inner(tokenizeBool(tok.content));
				return inner.parseExpr();
			}
			BoolExpr leaf; leaf.op = BoolExpr::LEAF; leaf.query = tok.content;
			return leaf;
		}
		return BoolExpr(); // vacío
	}
};

static BoolExpr parseBoolExpr(const string& s) {
	BoolParser p(tokenizeBool(s));
	return p.parseExpr();
}

static vector<bool> evalBoolExpr(
	const BoolExpr& e,
	const vector<string>& dictionary,
	const vector<string>& raw_dict,
	const unordered_map<string, string>& normToRaw,
	const map<int, vector<string>>& dictByLen
) {
	int N = (int)dictionary.size();
	if (e.op == BoolExpr::LEAF) {
		return runLeafQuery(e.query, dictionary, raw_dict, normToRaw, dictByLen);
	}
	if (e.op == BoolExpr::NOT_OP) {
		auto inner = evalBoolExpr(e.children[0], dictionary, raw_dict, normToRaw, dictByLen);
		vector<bool> res(N); for (int i = 0; i < N; i++) res[i] = !inner[i];
		return res;
	}
	if (e.children.size() < 2) return vector<bool>(N, false);
	auto left = evalBoolExpr(e.children[0], dictionary, raw_dict, normToRaw, dictByLen);
	auto right = evalBoolExpr(e.children[1], dictionary, raw_dict, normToRaw, dictByLen);
	vector<bool> res(N);
	if (e.op == BoolExpr::AND_OP)  for (int i = 0; i < N; i++) res[i] = left[i] && right[i];
	else if (e.op == BoolExpr::OR_OP)   for (int i = 0; i < N; i++) res[i] = left[i] || right[i];
	else if (e.op == BoolExpr::DIFF_OP) for (int i = 0; i < N; i++) res[i] = left[i] && !right[i];
	return res;
}

// --- CONSULTAS ANIDADAS ---

// Comprueba si 'arg' empieza por una consulta anidada (expr entre paréntesis que NO sea un rango de patrón).
// Si sí, resuelve la consulta, llena 'words' con los resultados y 'after' con el texto restante.
static bool tryResolveNestedArg(
	const string& arg,
	const vector<string>& dictionary,
	const vector<string>& raw_dict,
	const unordered_map<string, string>& normToRaw,
	const map<int, vector<string>>& dictByLen,
	vector<string>& words,
	string& after
) {
	string a = arg;
	a.erase(0, a.find_first_not_of(" "));
	if (a.empty() || a[0] != '(') return false;

	// Buscar el ) que cierra el primero ( (respetando [ ] anidados)
	int depth = 0, bdepth = 0;
	size_t i = 0;
	for (; i < a.size(); i++) {
		if (a[i] == '[') bdepth++;
		else if (a[i] == ']' && bdepth > 0) bdepth--;
		else if (a[i] == '(' && bdepth == 0) depth++;
		else if (a[i] == ')' && bdepth == 0) { depth--; if (!depth) { i++; break; } }
	}
	if (depth != 0) return false;

	string inner = a.substr(1, i - 2);
	after = a.substr(i);
	after.erase(0, after.find_first_not_of(" "));

	// ¿Es un rango de patrón? (ej: "1,3,V")
	{
		auto p = split(inner, ',');
		bool is_range = (p.size() <= 3);
		if (is_range && !p.empty() && !p[0].empty())
			is_range = all_of(p[0].begin(), p[0].end(), [](unsigned char c) { return isdigit(c); });
		if (is_range && p.size() >= 2 && !p[1].empty())
			is_range = all_of(p[1].begin(), p[1].end(), [](unsigned char c) { return isdigit(c); });
		if (is_range && p.size() >= 3 && !p[2].empty())
			is_range = (p[2] == "V" || p[2] == "C");
		if (is_range) return false;
	}

	// Resolver como subconsulta
	// Caso especial: /rd n PATRON → limitar a n resultados aleatorios
	int nested_rd_n = -1; // -1 = no es /rd
	string inner_for_search = inner;
	{
		string t = inner; t.erase(0, t.find_first_not_of(" "));
		bool is_rd_n = (t.size() >= 7 && t.substr(0, 7) == "/random") ||
			(t.size() >= 3 && t.substr(0, 3) == "/rd" && (t.size() == 3 || t[3] == ' '));
		if (is_rd_n) {
			string rest_r = (t.substr(0, 7) == "/random") ? t.substr(7) : t.substr(3);
			rest_r.erase(0, rest_r.find_first_not_of(" "));
			size_t sp2 = rest_r.find(' ');
			string ftok = (sp2 != string::npos) ? rest_r.substr(0, sp2) : rest_r;
			if (!ftok.empty() && all_of(ftok.begin(), ftok.end(), [](unsigned char c) {return isdigit(c);})) {
				nested_rd_n = safeStoi(ftok);
				string pat_part = (sp2 != string::npos) ? rest_r.substr(sp2 + 1) : "";
				pat_part.erase(0, pat_part.find_first_not_of(" "));
				if (pat_part.empty()) pat_part = ".";
				if (!pat_part.empty() && pat_part[0] == '[') pat_part = ". " + pat_part;
				inner_for_search = pat_part;
			}
		}
	}

	vector<bool> matched = hasBoolOps(inner_for_search)
		? evalBoolExpr(parseBoolExpr(inner_for_search), dictionary, raw_dict, normToRaw, dictByLen)
		: runLeafQuery(inner_for_search, dictionary, raw_dict, normToRaw, dictByLen);
	for (size_t j = 0; j < dictionary.size(); j++)
		if (matched[j]) words.push_back(raw_dict[j]);

	// Aplicar selección aleatoria si era /rd n
	if (nested_rd_n > 0 && (int)words.size() > nested_rd_n) {
		static mt19937 rng_nested(random_device{}());
		shuffle(words.begin(), words.end(), rng_nested);
		words.resize(nested_rd_n);
	}
	return true;
}

// --- MAIN ---

int main() {
	SetConsoleOutputCP(65001); // UTF-8
	SetConsoleCP(65001);
	ios_base::sync_with_stdio(false); cin.tie(NULL);

	vector<string> dictionary, raw_dict;
	string currentDict = "default";

	loadDictionary(currentDict, raw_dict, dictionary);

	// RNG para /random
	mt19937 rng(random_device{}());

	// --- ESTRUCTURAS PARA /calembour ---
	unordered_map<string, string> normToRaw;   // norma → primera forma raw
	map<int, vector<string>> dictByLen;         // longitud → [palabras normalizadas]

	auto buildCalLookup = [&]() {
		normToRaw.clear();
		dictByLen.clear();
		for (size_t i = 0; i < dictionary.size(); i++) {
			if (!normToRaw.count(dictionary[i]))
				normToRaw[dictionary[i]] = raw_dict[i];
			dictByLen[(int)dictionary[i].size()].push_back(dictionary[i]);
		}
		};
	buildCalLookup();

	// Lambda: ejecuta una búsqueda y devuelve los resultados como vector de raw words
	auto runSearch = [&](const string& il) -> vector<string> {
		vector<PatternElement> elems2; vector<ResourceCondition> res2;
		int tol2 = 0; bool istot2 = false;
		bool perr = false;
		parseInput(il, elems2, res2, tol2, istot2, &perr);
		if (perr) return {};
		vector<string> out;
		for (size_t i = 0; i < dictionary.size(); i++) {
			const string& w = dictionary[i];
			if (w.length() >= 100) continue;
			int re = checkResources(w, raw_dict[i], res2);
			int rt = tol2;
			if (istot2) { if (re > tol2) continue; rt -= re; }
			else if (re > 0) continue;
			for (int r2 = 0; r2 <= (int)w.length(); r2++)
				for (int e2 = 0; e2 <= (int)elems2.size(); e2++)
					for (int t2 = 0; t2 <= rt; t2++) memo_buffer[r2][e2][t2] = -1;
			if (matchPattern(w, 0, elems2, 0, rt)) out.push_back(raw_dict[i]);
		}
		return out;
		};

	// Muestra bloques de resultados con salto de línea entre cada bloque
	auto displayBlocks = [&](const vector<pair<string, vector<string>>>& blocks) {
		int total = 0;
		for (size_t bi = 0; bi < blocks.size(); bi++) {
			if (bi > 0) cout << "\n";
			for (const string& r : blocks[bi].second) cout << "- " << r << "\n";
			total += (int)blocks[bi].second.size();
		}
		cout << "Total: " << total << "\n";
		};

	cout << "\n=== BUSCADOR DE PALABRAS ===\n";
	cout << "Diccionario activo: " << currentDict << "\n\n";
	cout << "Escribe /help para ayuda general, /commands para ver todos los comandos.\n";

	while (true) {
		cout << "\n> ";
		string inputLine; getline(cin, inputLine);

		string input = inputLine;
		input.erase(0, input.find_first_not_of(" \t\r\n"));
		size_t last = input.find_last_not_of(" \t\r\n");
		if (last != string::npos) input.erase(last + 1);

		if (input == "/exit" || input == "/ex") break;
		if (input.empty()) continue;

		try {

			// --- LÓGICA BOOLEANA ---
			if (hasBoolOps(input)) {
				BoolExpr expr = parseBoolExpr(input);
				vector<bool> bitmask = evalBoolExpr(expr, dictionary, raw_dict, normToRaw, dictByLen);
				int total = 0;
				for (size_t i = 0; i < dictionary.size(); i++)
					if (bitmask[i]) { cout << "- " << raw_dict[i] << "\n"; total++; }
				cout << "Total: " << total << endl;
				continue;
			}

			if (input == "/commands" || input == "/cmd") {
				cout << "\n--- LÓGICA BOOLEANA ---\n" << endl;
				cout << "Combina consultas entre paréntesis con operadores:" << endl;
				cout << "  (A) && (B)   ->  palabras en A y en B (intersección)" << endl;
				cout << "  (A) || (B)   ->  palabras en A o en B (unión)" << endl;
				cout << "  (A) - (B)    ->  palabras en A pero no en B (diferencia)" << endl;
				cout << "  !(A)         ->  palabras que NO están en A (complemento)" << endl;
				cout << "  Precedencia: ! > && > - > ||    Agrupables con (())" << endl;
				cout << "  Ejemplo: ((/cal SUMANDOBLE [>1]) - (* [C*])) || !(\\uni E)" << endl;
				cout << "\n--- LISTA DE COMANDOS ---\n" << endl;
				cout << "/random,        /rd   -> Ejecuta una búsqueda y devuelve n palabras al azar." << endl;
				cout << "  /rd n PATRON [R] m  -> n palabras aleatorias del resultado de PATRON [R] m" << endl;
				cout << "  Si no se indica PATRON, se usa '.' (todas las palabras)." << endl;
				cout << "/calembour,     /cal  -> Divide la palabra en trozos que estén en el diccionario." << endl;
				cout << "  /cal PALABRA           -> Solo divisiones exactas." << endl;
				cout << "  /cal PALABRA [R1,R2]   -> Solo segmentos que cumplan las restricciones." << endl;
				cout << "  /cal PALABRA [R1,R2] n -> Ídem con n errores totales permitidos." << endl;
				cout << "/anagram,       /ang  -> Busca anagramas de la palabra." << endl;
				cout << "  /ang PALABRA -> . [3A,1B,2R,L,P]" << endl;
				cout << "/paronomasia,   /par  -> Busca palabras con igual esqueleto consonántico." << endl;
				cout << "  /par COCHE -> C*CH* [2V*]   (vocales sustituidas por *)" << endl;
				cout << "/anasyllabic,   /ans  -> Busca palabras reordenando las sílabas de la original." << endl;
				cout << "  /ans PALABRA -> PABRALA || BRAPALA || BRALAPA || ..." << endl;
				cout << "/anaphora,      /anp  -> Busca palabras que empiecen por la palabra o letras dadas." << endl;
				cout << "  /anp PAL -> PAL." << endl;
				cout << "/epiphora,      /epi  -> Busca palabras que terminen por la palabra o letras dadas." << endl;
				cout << "  /epi BRA -> .BRA" << endl;
				cout << "/multisyllabic, /mul  -> Busca palabras con la misma estructura vocálica." << endl;
				cout << "  /mul PALABRITA -> .A.A.I.A. [4V*]" << endl;
				cout << "/univocalism,   /uni  -> Busca palabras que solo contengan la vocal indicada." << endl;
				cout << "  /uni E -> . [E,0A,0I,0O,0U]" << endl;
				cout << "/assonant,      /aso  -> Busca rima asonante (vocales del sufijo tónico coinciden)." << endl;
				cout << "  /aso corazón -> palabras cuya estructura vocálica final es O-O" << endl;
				cout << "/consonant,     /con  -> Busca rima consonante (sufijo exacto desde vocal tónica)." << endl;
				cout << "  /con corazón -> palabras que terminan en '-azón'" << endl;
				cout << "/wordplay,      /wp   -> Busca iterando la tolerancia hasta encontrar resultados nuevos." << endl;
				cout << "  /wp PALABRA -> prueba PALABRA 1, si no hay resultados PALABRA 2, ..." << endl;
				cout << "\n--- Todos los comandos admiten restricciones [] y tolerancia n ---\n" << endl;
				cout << "/help,          /hp   -> Explicación general del buscador." << endl;
				cout << "/pattern,       /pat  -> Cómo definir la estructura (comodines y rangos)." << endl;
				cout << "/restriction,   /res  -> Cómo usar filtros entre corchetes []." << endl;
				cout << "/tolerance,     /tol  -> Cómo permitir errores en la búsqueda." << endl;
				cout << "/nested,        /nes  -> Cómo realizar busquedas anidadas." << endl;
				cout << "/load,          /ld   -> Muestra o cambia el diccionario activo." << endl;
				cout << "/exit,          /ex   -> Cierra la aplicación." << endl;
				continue;
			}

			// --- DETECCIÓN DE COMANDOS ---
			bool isAn = (input.substr(0, 8) == "/anagram" || (input.substr(0, 4) == "/ang" && (input.size() == 4 || input[4] == ' ')));
			bool isPar = (input.substr(0, 12) == "/paronomasia" || (input.substr(0, 4) == "/par" && (input.size() == 4 || input[4] == ' ')));
			bool isAns = (input.substr(0, 12) == "/anasyllabic" || (input.substr(0, 4) == "/ans" && (input.size() == 4 || input[4] == ' ')));
			bool isAnp = (input.substr(0, 9) == "/anaphora" || (input.substr(0, 4) == "/anp" && (input.size() == 4 || input[4] == ' ')));
			bool isEpi = (input.substr(0, 9) == "/epiphora" || (input.substr(0, 4) == "/epi" && (input.size() == 4 || input[4] == ' ')));
			bool isMul = (input.substr(0, 14) == "/multisyllabic" || (input.substr(0, 4) == "/mul" && (input.size() == 4 || input[4] == ' ')));
			bool isUni = (input.substr(0, 12) == "/univocalism" || (input.substr(0, 4) == "/uni" && (input.size() == 4 || input[4] == ' ')));
			bool isAso = (input.size() >= 9 && input.substr(0, 9) == "/assonant") || (input.size() >= 4 && input.substr(0, 4) == "/aso" && (input.size() == 4 || input[4] == ' '));
			bool isCon = (input.size() >= 10 && input.substr(0, 10) == "/consonant") || (input.size() >= 4 && input.substr(0, 4) == "/con" && (input.size() == 4 || input[4] == ' '));

			// --- DETECCIÓN DE /random (/rd) ---
			bool isRd = (input.substr(0, 7) == "/random" || (input.substr(0, 3) == "/rd" && (input.size() == 3 || input[3] == ' ')));
			int rd_n = 1;           // número de palabras a devolver
			string rd_pattern = ""; // patrón (vacío = ".")

			if (isRd) {
				// Extraer el resto del comando
				string rest = (input.substr(0, 7) == "/random") ? input.substr(7) : input.substr(3);
				rest.erase(0, rest.find_first_not_of(" "));

				// El primer token debe ser n (número entero)
				// Si el primer token no es un número, asumimos n=1 y todo es el patrón
				size_t sp = rest.find(' ');
				string first_token = (sp != string::npos) ? rest.substr(0, sp) : rest;
				string after_first = (sp != string::npos) ? rest.substr(sp + 1) : "";

				bool first_is_number = !first_token.empty() &&
					all_of(first_token.begin(), first_token.end(), [](unsigned char c) { return isdigit(c); });

				if (first_is_number) {
					rd_n = safeStoi(first_token);
					rd_pattern = after_first;
				}
				else {
					rd_n = 1;
					rd_pattern = rest;
				}

				// Si no hay patrón, usar "." (cualquier palabra)
				if (rd_pattern.empty()) rd_pattern = ".";
				// Si el patrón empieza por '[', no tiene estructura: añadir '.' implícito
				if (!rd_pattern.empty() && rd_pattern[0] == '[') rd_pattern = ". " + rd_pattern;

				// Construir el inputLine para la búsqueda normal
				inputLine = rd_pattern;
			}

			// Vector para almacenar todos los patrones a evaluar en esta ronda
			vector<string> patterns_to_run;

			if (isAso || isCon) {
				int plen = 4;
				if (isAso && input.size() >= 9 && input.substr(0, 9) == "/assonant") plen = 9;
				else if (isCon && input.size() >= 10 && input.substr(0, 10) == "/consonant") plen = 10;
				string rest_full = input.substr(plen);
				rest_full.erase(0, rest_full.find_first_not_of(" "));

				// Helper: de un 'rest' extrae word, extra_r, tolerance_str y devuelve inputLine para aso/con
				auto computeRhymeIL = [&](const string& r) -> string {
					string w2, extra_r2 = "", tol2 = "", rest2 = r;
					size_t bs = rest2.find('[');
					size_t ls = rest2.find_last_of(" ");
					if (bs != string::npos) {
						size_t be = rest2.find(']', bs);
						if (be != string::npos) { w2 = rest2.substr(0, bs); extra_r2 = rest2.substr(bs + 1, be - bs - 1); tol2 = rest2.substr(be + 1); tol2.erase(0, tol2.find_first_not_of(" ")); }
					}
					else if (ls != string::npos) {
						string pn = rest2.substr(ls + 1); string cn = pn; if (!cn.empty() && cn.back() == '*') cn.pop_back();
						if (!cn.empty() && all_of(cn.begin(), cn.end(), [](unsigned char c) {return isdigit(c);})) { w2 = rest2.substr(0, ls); tol2 = pn; }
						else w2 = rest2;
					}
					else w2 = rest2;
					w2.erase(remove(w2.begin(), w2.end(), ' '), w2.end());
					string rhyme = getRhymeSuffix(w2);
					if (rhyme.empty()) return "";
					string il2;
					if (isCon) il2 = "." + rhyme;
					else { il2 = ".(0,,C)"; for (char c : rhyme) if (isVowel(c)) il2 += string(1, c) + "(0,,C)"; }
					if (!extra_r2.empty()) il2 += " [" + extra_r2 + "]";
					if (!tol2.empty()) il2 += " " + tol2;
					return il2;
					};

				vector<string> nested_words_ac; string nested_after_ac;
				bool is_nested_ac = tryResolveNestedArg(rest_full, dictionary, raw_dict, normToRaw, dictByLen, nested_words_ac, nested_after_ac);
				if (is_nested_ac) {
					if (nested_words_ac.empty()) { cout << "(La consulta anidada no devolvió resultados)" << endl; continue; }
					vector<pair<string, vector<string>>> blocks;
					for (const string& nw : nested_words_ac) {
						string r = nw + (nested_after_ac.empty() ? "" : " " + nested_after_ac);
						string il = computeRhymeIL(r);
						if (il.empty()) { cout << "(No se pudo determinar la rima de '" << nw << "')\n"; blocks.push_back({ nw,{} }); continue; }
						blocks.push_back({ nw, runSearch(il) });
					}
					displayBlocks(blocks);
					continue;
				}
				// Caso normal (una sola palabra)
				string il = computeRhymeIL(rest_full);
				if (il.empty()) { cout << "(No se pudo determinar la rima de '" << rest_full << "')" << endl; continue; }
				string rhyme_disp = getRhymeSuffix(rest_full.substr(0, rest_full.find(' ')));
				if (isCon) cout << "(Buscando rima consonante con sufijo: " << rhyme_disp << ")\n";
				else { string vd; for (char c : rhyme_disp) if (isVowel(c)) vd += c; cout << "(Buscando rima asonante con vocales: " << vd << ")\n"; }
				inputLine = il;
			}

			// Detección de comandos inválidos (empieza por / pero no es reconocido)
			if (input[0] == '/' && !isAn && !isPar && !isAns && !isAnp && !isEpi && !isMul && !isUni
				&& !isAso && !isCon
				&& input != "/help" && input != "/hp"
				&& input != "/pattern" && input != "/pat"
				&& input != "/restriction" && input != "/res"
				&& input != "/tolerance" && input != "/tol"
				&& input != "/nested" && input!="/nes"
				&& input != "/commands" && input != "/cmd"
				&& !(input.size() >= 5 && input.substr(0, 5) == "/load") && !(input.size() >= 3 && input.substr(0, 3) == "/ld")
				&& !(input.size() >= 7 && input.substr(0, 7) == "/random") && !(input.size() >= 3 && input.substr(0, 3) == "/rd" && (input.size() == 3 || input[3] == ' '))
				&& !(input.size() >= 10 && input.substr(0, 10) == "/calembour") && !(input.size() >= 4 && input.substr(0, 4) == "/cal" && (input.size() == 4 || input[4] == ' '))
				&& !(input.size() >= 9 && input.substr(0, 9) == "/wordplay") && !(input.size() >= 3 && input.substr(0, 3) == "/wp" && (input.size() == 3 || input[3] == ' '))
				&& input != "/exit" && input != "/ex") {
				cout << "(Sintaxis inválida o comando desconocido. Usa /help o /commands para ver las opciones.)" << endl;
				continue;
			}

			// extractWordArgs: alias local para extractWordRestrTol (definida globalmente)
			auto extractWordArgs = extractWordRestrTol;

			if (isAn || isPar) {
				bool isAnagram = isAn;
				string rest_full;
				if (isAn) rest_full = (input.substr(0, 8) == "/anagram") ? input.substr(8) : input.substr(4);
				else      rest_full = (input.substr(0, 12) == "/paronomasia") ? input.substr(12) : input.substr(4);
				rest_full.erase(0, rest_full.find_first_not_of(" "));

				auto computeAnIL = [&](const string& r) -> string {
					auto [word, extra_r, tol_str] = extractWordArgs(r);
					string nw = normalizeWord(word);
					if (isAnagram) {
						// Normalizar la tolerancia para que siempre incluya '*' (es total)
						string ts = tol_str;
						if (!ts.empty()) {
							string tmp = ts;
							if (tmp.back() == '*') tmp.pop_back();
							if (all_of(tmp.begin(), tmp.end(), [](unsigned char c) { return isdigit(c); }))
								ts = tmp + "*";
						}
						map<char, int> cnt;
						for (char c : nw) cnt[c]++;
						string exp = ". [";
						for (auto& [ch, co] : cnt) exp += to_string(co) + ch + ",";
						exp += to_string(nw.length());
						if (!extra_r.empty()) exp += "," + extra_r;
						exp += "] " + ts;
						return exp;
					}
					else {
						// Esqueleto consonántico: consonantes en su posición, vocales → '*'
						// Ejemplo: COCHE → C*CH* [2V*]
						string pat = ""; int vow = 0; string cgrp = "";
						for (char c : nw) {
							if (isVowel(c)) { pat += cgrp + "*"; cgrp = ""; vow++; }
							else cgrp += c;
						}
						pat += cgrp; // consonantes finales
						string exp = pat + " [" + to_string(vow) + "V*";
						if (!extra_r.empty()) exp += "," + extra_r;
						exp += "] " + tol_str;
						return exp;
					}
					};

				vector<string> nw_an; string na_an;
				if (tryResolveNestedArg(rest_full, dictionary, raw_dict, normToRaw, dictByLen, nw_an, na_an)) {
					if (nw_an.empty()) { cout << "(La consulta anidada no devolvió resultados)" << endl; continue; }
					vector<pair<string, vector<string>>> blocks;
					for (const string& nw : nw_an) {
						string r = nw + (na_an.empty() ? "" : " " + na_an);
						blocks.push_back({ nw, runSearch(computeAnIL(r)) });
					}
					displayBlocks(blocks); continue;
				}
				inputLine = computeAnIL(rest_full);
			}

			if (isAns) {
				string rest_full = (input.substr(0, 12) == "/anasyllabic") ? input.substr(12) : input.substr(4);
				rest_full.erase(0, rest_full.find_first_not_of(" "));

				auto computeAnsPatterns = [&](const string& r) -> vector<string> {
					auto [word, extra_r, tol_str] = extractWordArgs(r);
					string nw = normalizeWord(word);
					vector<string> syls = getSyllables(nw); sort(syls.begin(), syls.end());
					vector<string> result;
					do {
						string p = ""; for (const string& s : syls) p += s;
						if (!extra_r.empty()) p += " [" + extra_r + "]";
						if (!tol_str.empty()) p += " " + tol_str;
						result.push_back(p);
					} while (next_permutation(syls.begin(), syls.end()));
					return result;
					};

				vector<string> nw_ans; string na_ans;
				if (tryResolveNestedArg(rest_full, dictionary, raw_dict, normToRaw, dictByLen, nw_ans, na_ans)) {
					if (nw_ans.empty()) { cout << "(La consulta anidada no devolvió resultados)" << endl; continue; }
					vector<pair<string, vector<string>>> blocks;
					for (const string& nw : nw_ans) {
						string r = nw + (na_ans.empty() ? "" : " " + na_ans);
						auto pats = computeAnsPatterns(r);
						cout << "(Buscando en " << pats.size() << " permutaciones para '" << nw << "'...)\n";
						// Union de todas las permutaciones
						unordered_set<string> seen;
						vector<string> res_nw;
						for (const string& pLine : pats) {
							auto partial = runSearch(pLine);
							for (const string& pw : partial) if (seen.insert(normalizeWord(pw)).second) res_nw.push_back(pw);
						}
						blocks.push_back({ nw, res_nw });
					}
					displayBlocks(blocks); continue;
				}
				patterns_to_run = computeAnsPatterns(rest_full);
				cout << "(Buscando en " << patterns_to_run.size() << " permutaciones silábicas...)\n";
			}

			if (isAnp || isEpi || isMul || isUni) {
				string rest_full;
				if (isAnp)      rest_full = (input.substr(0, 9) == "/anaphora") ? input.substr(9) : input.substr(4);
				else if (isEpi) rest_full = (input.substr(0, 9) == "/epiphora") ? input.substr(9) : input.substr(4);
				else if (isMul) rest_full = (input.substr(0, 14) == "/multisyllabic") ? input.substr(14) : input.substr(4);
				else            rest_full = (input.substr(0, 12) == "/univocalism") ? input.substr(12) : input.substr(4);
				rest_full.erase(0, rest_full.find_first_not_of(" "));

				auto computeAnpIL = [&](const string& r) -> string {
					auto [word, extra_r, tol_str] = extractWordArgs(r);
					string nw = normalizeWord(word);
					string exp;
					if (isAnp) {
						exp = nw + "."; if (!extra_r.empty()) exp += " [" + extra_r + "]"; if (!tol_str.empty()) exp += " " + tol_str;
					}
					else if (isEpi) {
						exp = "." + nw; if (!extra_r.empty()) exp += " [" + extra_r + "]"; if (!tol_str.empty()) exp += " " + tol_str;
					}
					else if (isMul) {
						string pat = "."; int vow = 0;
						for (char c : nw) if (isVowel(c)) { pat += c; pat += "."; vow++; }
						exp = pat + " [" + to_string(vow) + "V*"; if (!extra_r.empty()) exp += "," + extra_r; exp += "]"; if (!tol_str.empty()) exp += " " + tol_str;
					}
					else { // isUni
						char v = '\0';
						for (char c : nw) if (isVowel(c)) { v = c; break; }
						if (v) {
							exp = ". [" + string(1, v);
							string av = "AEIOU";
							for (char c : av) if (c != v) exp += ",0" + string(1, c);
							if (!extra_r.empty()) exp += "," + extra_r;
							exp += "]";
							if (!tol_str.empty()) exp += " " + tol_str;
						}
					}
					return exp;
					};

				vector<string> nw_anp; string na_anp;
				if (tryResolveNestedArg(rest_full, dictionary, raw_dict, normToRaw, dictByLen, nw_anp, na_anp)) {
					if (nw_anp.empty()) { cout << "(La consulta anidada no devolvió resultados)" << endl; continue; }
					vector<pair<string, vector<string>>> blocks;
					for (const string& nw : nw_anp) {
						string r = nw + (na_anp.empty() ? "" : " " + na_anp);
						blocks.push_back({ nw, runSearch(computeAnpIL(r)) });
					}
					displayBlocks(blocks); continue;
				}
				inputLine = computeAnpIL(rest_full);
			}

			if (input == "/help" || input == "/hp") {

				cout << "\n--- BUSCADOR DE PALABRAS ---\n\n";
				cout << "Busca palabras en un diccionario usando un lenguaje de patrones.\n\n";
				cout << "Una búsqueda se define en una sola línea con hasta 3 partes:\n\n";
				cout << "  1) Estructura (obligatoria)  ->  describe la forma de la palabra\n";
				cout << "  2) Restricciones  [R1,R2,...] (opcional)  ->  filtros globales\n";
				cout << "  3) Tolerancia  n  o  n*  (opcional)  ->  errores permitidos\n\n";
				cout << "Cada parte es independiente y pueden combinarse libremente.\n\n";
				cout << "  Usa /pattern     para aprender a definir estructuras.\n";
				cout << "  Usa /restriction para aprender a usar filtros.\n";
				cout << "  Usa /tolerance   para entender la tolerancia a errores.\n";
				cout << "  Usa /nested      para entender las consultas anidadas.\n";
				cout << "  Usa /commands    para ver todos los comandos disponibles.\n";
				continue;
			}

			if (input == "/pattern" || input == "/pat") {

				cout << "\n--- 1. ESTRUCTURA DE LA PALABRA ---\n\n";
				cout << "Describe la forma interna de la palabra, de izquierda a derecha.\n";
				cout << "El patrón debe cubrir la palabra completa.\n\n";
				cout << "ELEMENTOS DISPONIBLES:\n";
				cout << "  *        ->  exactamente 1 letra cualquiera\n";
				cout << "  X        ->  la letra X exacta (ej: A, B, ~)\n";
				cout << "  .        ->  cualquier número de letras (incluido cero)\n\n";
				cout << "RANGOS (entre paréntesis):\n";
				cout << "  (n,m)    ->  entre n y m letras cualquiera\n";
				cout << "  (n,m,V)  ->  entre n y m vocales\n";
				cout << "  (n,m,C)  ->  entre n y m consonantes\n\n";
				cout << "NOTAS:\n";
				cout << "  - Si n está vacío, se asume 0\n";
				cout << "  - Si m está vacío, se asume infinito\n";
				cout << "  - Cada elemento consume letras consecutivas\n\n";
				cout << "EJEMPLOS:\n";
				cout << "  (1,2,C)A.   -> 1-2 consonantes, luego 'A', luego cualquier cosa\n";
				cout << "  CAS*        -> palabras de 4 letras que empiecen por CAS\n";
				continue;

			}



			if (input == "/restriction" || input == "/res") {
				cout << "\n--- 2. RESTRICCIONES [Filtros] ---\n\n";
				cout << "Se escriben entre corchetes después del patrón: PATRON [R1,R2,...]\n";
				cout << "Cada restricción tiene la forma: [operador][número][elemento]\n\n";
				cout << "OPERADORES: ==  >=  <=  >  <   (si se omite, se asume ==)\n\n";
				cout << "ELEMENTOS DISPONIBLES:\n";
				cout << "  V*       ->  número total de vocales\n";
				cout << "  C*       ->  número total de consonantes\n";
				cout << "  S*       ->  número de sílabas\n";
				cout << "  T*       ->  posición de la sílaba tónica desde la derecha\n";
				cout << "               (1 = aguda, 2 = llana, 3 = esdrújula)\n";
				cout << "  A-Z      ->  ocurrencias de una letra concreta\n";
				cout << "  (vacío)  ->  longitud total de la palabra\n\n";
				cout << "EJEMPLOS:\n";
				cout << "  [3S*]          ->  palabras de exactamente 3 sílabas\n";
				cout << "  [>=2V*,0K]     ->  al menos 2 vocales y ninguna K\n";
				cout << "  A. [2S*, <7]   ->  empieza por A, 2 sílabas y menos de 7 letras\n";
				cout << "  . [T*==1]      ->  palabras agudas\n";
				continue;
			}



			if (input == "/tolerance" || input == "/tol") {
				cout << "\n--- 3. TOLERANCIA A ERRORES ---\n\n";
				cout << "Número al final del patrón que indica cuántos errores se permiten.\n\n";
				cout << "Se considera un error, por ejemplo:\n";
				cout << "  - Una letra que no cumple el tipo esperado (vocal en lugar de consonante, etc.)\n";
				cout << "  - Letras que sobran o faltan para satisfacer un rango\n\n";
				cout << "TOLERANCIA PARCIAL (n):\n";
				cout << "  Solo se aplica al patrón. Las restricciones [] son siempre obligatorias.\n\n";
				cout << "TOLERANCIA TOTAL (n*):\n";
				cout << "  El asterisco hace que la tolerancia sea global: los errores en las\n";
				cout << "  restricciones [] también consumen del límite.\n\n";
				cout << "EJEMPLOS:\n";
				cout << "  HOLA 1       ->  permite 1 error en el patrón (BOLA, OLA, HOLI...)\n";
				cout << "  . [3A] 1*    ->  busca 3 letras 'A', pero acepta 2 o 4 con 1 error total\n";
				continue;
			}

			if (input == "/nested" || input == "/nes") {
				cout << "\n--- CONSULTAS ANIDADAS ---\n\n";
				cout << "Una consulta anidada es una búsqueda escrita entre paréntesis que se evalúa primero,\n";
				cout << "y cuyo resultado (una lista de palabras) se usa como entrada de otro comando.\n\n";
				cout << "Permite encadenar búsquedas complejas y componer operaciones en varios niveles.\n\n";

				cout << "SINTAXIS GENERAL:\n\n";
				cout << "  COMANDO (CONSULTA_INTERNA) [RESTRICCIONES] n\n\n";

				cout << "La CONSULTA_INTERNA puede ser cualquier búsqueda válida:\n";
				cout << "  - un patrón normal\n";
				cout << "  - un comando (/cal, /aso, /an, /mul, etc.)\n";
				cout << "  - una expresión booleana\n";
				cout << "  - una búsqueda aleatoria (/rd)\n\n";

				cout << "EJEMPLOS BÁSICOS:\n\n";
				cout << "  /cal (CASA)\n";
				cout << "    -> divide CASA solo usando palabras que estén en el diccionario\n\n";
				cout << "  /aso (AMOR)\n";
				cout << "    -> busca rima asonante con cada resultado de la consulta interna\n\n";

				cout << "EJEMPLOS CON /random:\n\n";
				cout << "  /cal (/rd 3 [E])\n";
				cout << "    -> elige 3 palabras al azar que tengan E y aplica /cal a cada una\n\n";
				cout << "  /con (/rd 5 .)\n";
				cout << "    -> rima consonante con 5 palabras aleatorias del diccionario\n\n";

				cout << "EJEMPLOS AVANZADOS:\n\n";
				cout << "  /cal ((/rd 2 [>=3V*]) || (/rd 2 [>=3C*]))\n";
				cout << "    -> mezcla dos conjuntos aleatorios y divide cada palabra\n\n";
				cout << "  /anp (/rd 5 . [3S*])\n";
				cout << "    -> busca anáforas de 5 palabras trisílabas al azar\n\n";
				cout << "  /aso ((/cal SOL) || (/cal LUNA))\n";
				cout << "    -> rima asonante con todas las palabras obtenidas de ambos calembours\n\n";

				cout << "NOTAS IMPORTANTES:\n\n";
				cout << "  - La consulta interna se evalúa completamente antes del comando externo.\n";
				cout << "  - Puede contener operadores booleanos: &&, ||, -, !\n";
				cout << "  - Puede incluir /random (/rd n).\n";
				cout << "  - Los paréntesis de consultas NO se confunden con rangos de patrón (n,m,V).\n";
				cout << "  - Si la consulta anidada no devuelve resultados, el comando externo no se ejecuta.\n\n";

				cout << "Los comandos que aceptan consultas anidadas incluyen, entre otros:\n";
				cout << "  /cal, /anagram, /paronomasia, /anasyllabic,\n";
				cout << "  /anaphora, /epiphora, /multisyllabic, /univocalism,\n";
				cout << "  /assonant, /consonant, /wordplay\n";
			}

			if ((input.size() >= 5 && input.substr(0, 5) == "/load") || (input.size() >= 3 && input.substr(0, 3) == "/ld")) {
				string rest = (input.substr(0, 5) == "/load") ? input.substr(5) : input.substr(3);
				rest.erase(0, rest.find_first_not_of(" \t"));
				if (rest.empty()) listDictionaries();
				else if (loadDictionary(rest, raw_dict, dictionary)) { currentDict = rest; buildCalLookup(); }
				continue;
			}

			// --- DETECCIÓN DE /calembour (/cal) ---
			bool isCal = (input.substr(0, 10) == "/calembour" || (input.substr(0, 4) == "/cal" && (input.size() == 4 || input[4] == ' ')));

			if (isCal) {
				string rest = (input.substr(0, 10) == "/calembour") ? input.substr(10) : input.substr(4);
				rest.erase(0, rest.find_first_not_of(" "));

				// Detectar consulta anidada: /cal (/rd 2 [E]) [>1]
				vector<string> nw_cal; string na_cal;
				bool cal_nested = tryResolveNestedArg(rest, dictionary, raw_dict, normToRaw, dictByLen, nw_cal, na_cal);
				if (cal_nested) {
					if (nw_cal.empty()) { cout << "(La consulta anidada no devolvió resultados)" << endl; continue; }
					rest = ""; // se reasignará por cada palabra
				}

				// Lista de (word, cal_restr, cal_n) a procesar
				vector<tuple<string, string, int>> cal_tasks;
				auto parseCal = [](const string& r) -> tuple<string, string, int> {
					int cal_n = 0; string cal_word = r, cal_restr = "";
					size_t bs = r.find('[');
					if (bs != string::npos) {
						size_t be = r.find(']', bs);
						if (be != string::npos) {
							cal_word = r.substr(0, bs); cal_restr = r.substr(bs + 1, be - bs - 1);
							string rem = r.substr(be + 1); rem.erase(0, rem.find_first_not_of(" "));
							if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char c) {return isdigit(c);})) cal_n = stoi(rem);
						}
					}
					else {
						size_t lsp = r.find_last_of(" ");
						if (lsp != string::npos) {
							string pn = r.substr(lsp + 1);
							if (!pn.empty() && all_of(pn.begin(), pn.end(), [](unsigned char c) {return isdigit(c);})) { cal_n = stoi(pn); cal_word = r.substr(0, lsp); }
						}
					}
					cal_word.erase(remove(cal_word.begin(), cal_word.end(), ' '), cal_word.end());
					return { cal_word, cal_restr, cal_n };
					};

				if (cal_nested) {
					for (const string& nw : nw_cal)
						cal_tasks.push_back(parseCal(nw + (na_cal.empty() ? "" : " " + na_cal)));
				}
				else {
					cal_tasks.push_back(parseCal(rest));
				}

				bool first_cal_block = true;
				int total_cal_all = 0;

				for (auto& [cal_word, cal_restr, cal_n] : cal_tasks) {
					string normCal = normalizeWord(cal_word);
					if (normCal.empty()) { cout << "(Indica una palabra para /cal)" << endl; continue; }
					if ((int)normCal.size() > 20) { cout << "(Palabra demasiado larga, max 20: " << cal_word << ")" << endl; continue; }

					if (!first_cal_block) cout << "\n";
					first_cal_block = false;

					// Parsear restricciones para los segmentos
					vector<ResourceCondition> cal_resources = parseConditionList(cal_restr);

					int L = (int)normCal.size();
					vector<vector<pair<int, string>>> best(L, vector<pair<int, string>>(L + 1, { INT_MAX, "" }));
					for (int i = 0; i < L; i++) for (int j = i + 1; j <= L; j++) {
						string part = normCal.substr(i, j - i); int plen2 = (int)part.size();
						auto it_exact = normToRaw.find(part);
						if (it_exact != normToRaw.end()) {
							if (cal_resources.empty() || checkResources(part, it_exact->second, cal_resources) == 0) best[i][j] = { 0, it_exact->second };
							continue;
						}
						if (cal_n == 0) continue;
						int be = cal_n + 1; string bw = "";
						for (int len = max(1, plen2 - cal_n); len <= plen2 + cal_n; len++) {
							auto il = dictByLen.find(len); if (il == dictByLen.end()) continue;
							for (const string& w2 : il->second) {
								if (!cal_resources.empty() && checkResources(w2, normToRaw.at(w2), cal_resources) > 0) continue;
								int d = levenshtein(part, w2, be - 1);
								if (d < be) { be = d; bw = normToRaw.at(w2); if (be == 0) break; }
							}
							if (be == 0) break;
						}
						if (be <= cal_n) best[i][j] = { be, bw };
					}

					vector<vector<pair<string, int>>> all_results;
					function<void(int, int, vector<pair<string, int>>&)> cal_search =
						[&](int pos, int err_left, vector<pair<string, int>>& current) {
						if (pos == L) { if ((int)current.size() >= 2) all_results.push_back(current); return; }
						for (int end = pos + 1; end <= L; end++) {
							int berr = best[pos][end].first; const string& braw = best[pos][end].second;
							if (berr == INT_MAX || berr > err_left) continue;
							current.push_back({ braw, berr }); cal_search(end, err_left - berr, current); current.pop_back();
						}
						};
					vector<pair<string, int>> cur; cal_search(0, cal_n, cur);

					if (all_results.empty()) { cout << "(Sin resultados para " << cal_word << ")" << endl; }
					else {
						for (auto& parts2 : all_results) {
							for (int k = 0; k < (int)parts2.size(); k++) {
								if (k > 0) cout << " ";
								cout << parts2[k].first;
								if (parts2[k].second > 0) cout << "(~" << parts2[k].second << ")";
							}
							cout << "\n";
						}
						total_cal_all += (int)all_results.size();
					}
				}
				if (!cal_tasks.empty()) cout << "Total: " << total_cal_all << endl;
				continue;
			}
			// --- CONFIGURACIÓN DE WORDPLAY ---
			bool is_wordplay = false;
			string wp_word = "";
			string wp_restr = "";
			int wp_n = 1;
			bool wp_asterisk = false;

			bool isWp = (input.substr(0, 9) == "/wordplay" || (input.substr(0, 3) == "/wp" && (input.size() == 3 || input[3] == ' ')));

			if (isWp) {
				is_wordplay = true;
				string rest = (input.substr(0, 9) == "/wordplay") ? input.substr(9) : input.substr(3);
				rest.erase(0, rest.find_first_not_of(" "));

				size_t b_start = rest.find('[');
				size_t last_space = rest.find_last_of(" ");

				if (b_start != string::npos) {
					wp_word = rest.substr(0, b_start);
					size_t b_end = rest.find(']', b_start);
					if (b_end != string::npos) {
						wp_restr = rest.substr(b_start + 1, b_end - b_start - 1);
						string possible_n = rest.substr(b_end + 1);
						possible_n.erase(0, possible_n.find_first_not_of(" "));
						if (!possible_n.empty()) {
							if (possible_n.back() == '*') { wp_asterisk = true; possible_n.pop_back(); }
							if (!possible_n.empty() && all_of(possible_n.begin(), possible_n.end(), [](unsigned char c) { return isdigit(c); })) {
								wp_n = safeStoi(possible_n);
							}
						}
					}
				}
				else if (last_space != string::npos) {
					string possible_n = rest.substr(last_space + 1);
					string check_n = possible_n;
					if (!check_n.empty() && check_n.back() == '*') { wp_asterisk = true; check_n.pop_back(); }

					if (!check_n.empty() && all_of(check_n.begin(), check_n.end(), [](unsigned char c) { return isdigit(c); })) {
						wp_word = rest.substr(0, last_space);
						wp_n = safeStoi(check_n);
					}
					else wp_word = rest;
				}
				else {
					wp_word = rest;
				}
				wp_word.erase(remove(wp_word.begin(), wp_word.end(), ' '), wp_word.end());
			}

			// --- LÓGICA DE BÚSQUEDA ---
			while (true) {
				if (is_wordplay) {
					inputLine = wp_word;
					if (!wp_restr.empty()) inputLine += " [" + wp_restr + "]";
					inputLine += " " + to_string(wp_n) + (wp_asterisk ? "*" : "");
					patterns_to_run = { inputLine };
				}
				else if (!isAns) {
					patterns_to_run = { inputLine };
				}

				vector<string> results;
				// Usamos un vector booleano para evitar duplicados si una palabra matchea más de un patrón (O(1) lookup)
				vector<bool> matched_words(dictionary.size(), false);

				for (const string& pLine : patterns_to_run) {
					vector<PatternElement> elems;
					vector<ResourceCondition> resources;
					int tolerance = 0;
					bool is_total = false;
					bool parse_err = false;

					parseInput(pLine, elems, resources, tolerance, is_total, &parse_err);
					if (parse_err) { cout << "(Sintaxis inválida en el patrón. El programa continúa.)" << endl; break; }

					for (size_t i = 0; i < dictionary.size(); ++i) {
						if (matched_words[i]) continue; // Ya fue encontrada en otra permutación

						const string& w = dictionary[i];
						if (w.length() >= 100) continue;

						int res_errors = checkResources(w, raw_dict[i], resources);
						int remaining_tolerance = tolerance;

						if (is_total) {
							if (res_errors > tolerance) continue;
							remaining_tolerance -= res_errors;
						}
						else {
							if (res_errors > 0) continue;
						}

						for (int r = 0; r <= (int)w.length(); ++r)
							for (int e = 0; e <= (int)elems.size(); ++e)
								for (int t = 0; t <= remaining_tolerance; ++t) memo_buffer[r][e][t] = -1;

						if (matchPattern(w, 0, elems, 0, remaining_tolerance)) {
							matched_words[i] = true;
							results.push_back(raw_dict[i]);
						}
					}
				}

				// Control de ciclo para Wordplay
				if (is_wordplay) {
					bool empty_or_self = false;
					if (results.empty()) {
						empty_or_self = true;
					}
					else if (results.size() == 1 && normalizeWord(results[0]) == normalizeWord(wp_word)) {
						empty_or_self = true;
					}

					if (empty_or_self && wp_n < 99) {
						wp_n++;
						continue;
					}
				}

				// --- SELECCIÓN ALEATORIA para /random ---
				if (isRd) {
					if (results.empty()) {
						cout << "(Sin resultados para el patron dado)" << endl;
					}
					else {
						// Mezclar y tomar los primeros rd_n (o todos si hay menos)
						shuffle(results.begin(), results.end(), rng);
						int take = (std::min)(rd_n, (int)results.size());
						cout << "(Mostrando " << take << " de " << results.size() << " resultados)\n";
						for (int i = 0; i < take; ++i) {
							cout << "- " << results[i] << "\n";
						}
					}
					break;
				}

				// Imprimir resultados
				for (const string& res : results) {
					cout << "- " << res << "\n";
				}
				cout << "Total: " << results.size() << endl;

				if (is_wordplay) {
					cout << "(B\xC3\xBAsqueda completada con n = " << wp_n << ")" << endl;
				}

				break;
			}
		}
		catch (...) {
			cout << "(Sintaxis inválida. El programa continúa.)" << endl;
		}
	}
	return 0;
}