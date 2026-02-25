#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <windows.h>
#include <filesystem>
#include <map>

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
		if (!file) { cout << "\nError: No existe " << txtFile << endl; return false; }
		string line;
		while (getline(file, line)) {
			if (!line.empty()) {
				raw.push_back(line);
				norm.push_back(normalizeWord(line));
			}
		}
		saveBinaryCache(binFile, raw, norm);
	}
	cout << "[OK] " << norm.size() << " palabras." << endl;
	return true;
}

// --- LÓGICA DE BÚSQUEDA ---

void parseInput(string input, vector<PatternElement>& elems, vector<ResourceCondition>& resources, int& tolerance, bool& is_total) {
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
					tolerance = stoi(rem);
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
					tolerance = stoi(rem);
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
			int min_c = (parts.size() > 0 && !parts[0].empty()) ? stoi(parts[0]) : 0;
			int max_c = (parts.size() > 1 && !parts[1].empty()) ? stoi(parts[1]) : 99;
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

	if (!r_str.empty()) {
		auto parts = split(r_str, ',');
		for (string cond : parts) {
			cond.erase(0, cond.find_first_not_of(" "));
			string op = ""; int idx = 0;
			while (idx < (int)cond.length() && string("<>=").find(cond[idx]) != string::npos) op += cond[idx++];
			string num_str = "";
			while (idx < (int)cond.length() && isdigit((unsigned char)cond[idx])) num_str += cond[idx++];
			string target = cond.substr(idx);
			target.erase(0, target.find_first_not_of(" "));
			for (auto& c_tgt : target) c_tgt = (char)toupper((unsigned char)c_tgt);

			int num = 1;
			if (op.empty() && num_str.empty()) { op = ">="; num = 1; }
			else if (op.empty()) { op = "=="; num = stoi(num_str); }
			else if (!num_str.empty()) num = stoi(num_str);
			resources.push_back({ op, num, target });
		}
	}
}

int checkResources(const string& word, const vector<ResourceCondition>& resources) {
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
		else if (res.target == "") val = (int)word.length();
		else val = l_count[(unsigned char)res.target[0]];

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
		int m_chars = min(L, E.max_count);
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

// --- MAIN ---

int main() {
	SetConsoleOutputCP(65001); // UTF-8
	SetConsoleCP(65001);
	ios_base::sync_with_stdio(false); cin.tie(NULL);

	vector<string> dictionary, raw_dict;
	string currentDict = "default";

	loadDictionary(currentDict, raw_dict, dictionary);

	cout << "\nDiccionario actual: " << currentDict << endl << endl;
	cout << "Escribe /help para obtener ayuda." << endl;

	while (true) {
		cout << "\nPatr\xC3\xB3n:\n\n";
		string inputLine; getline(cin, inputLine);

		string input = inputLine;
		input.erase(0, input.find_first_not_of(" \t\r\n"));
		size_t last = input.find_last_not_of(" \t\r\n");
		if (last != string::npos) input.erase(last + 1);

		if (input == "/exit" || input == "/ex") break;
		if (input.empty()) continue;

		if (input == "/commands" || input == "/cmd") {
			cout << "\n--- LISTA DE COMANDOS ---" << endl;
			cout << "/anagram,       /ang  -> Busca anagramas de la palabra." << endl;
			cout << "	/ang PALABRA -> . [P,3A,L,B,R]" << endl;
			cout << "/paronomasia,   /par  -> Busca palabras con igual esqueleto consonantico." << endl;
			cout << "	/par PALABRA -> P*L*BR* [3V*]" << endl;
			cout << "/anasyllabic,   /ans  -> Busca palabras reordenando las silabas de la original." << endl;
			cout << "	/ans PALABRA -> PABRALA && BRAPALA && BRALAPA && ..." << endl;
			cout << "/anaphora,      /anp  -> Busca palabras que empiecen por la palabra o letras indicadas." << endl;
			cout << "	/anp PAL -> PAL." << endl;
			cout << "/epiphora,      /epi  -> Busca palabras que terminen por la palabra o letras indicadas." << endl;
			cout << "	/epi BRA -> .BRA" << endl;
			cout << "/multisyllabic, /mul  -> Busca palabras con la misma estructura vocalica." << endl;
			cout << "	/mul PALABRITA -> .A.A.I.A. [4V*]" << endl;
			cout << "/univocalism,   /uni  -> Busca palabras que solo contengan la vocal indicada." << endl;
			cout << "	/uni E -> . [E,0A,0I,0U,0O]" << endl;
			cout << "/wordplay,      /wp   -> Busca iterando la tolerancia hasta encontrar algo nuevo." << endl;
			cout << "	/wp PALABRA -> PALABRA 1, si no encuentra PALABRA 2,..." << endl;
			cout << "--- Todos los comandos anteriores pueden acompañarse de restricciones [] y tolerencia n ---" << endl;
			cout << "/help,          /hp   -> Explicacion general del buscador." << endl;
			cout << "/pattern,       /pat  -> Como definir la estructura (comodines y rangos)." << endl;
			cout << "/restriction,   /res  -> Como usar filtros entre corchetes []." << endl;
			cout << "/tolerance,     /tol  -> Como permitir errores en la busqueda." << endl;
			cout << "/load,          /ld   -> Muestra o cambia el diccionario actual." << endl;
			cout << "/exit,          /ex   -> Cierra la aplicacion." << endl;
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

		// Vector para almacenar todos los patrones a evaluar en esta ronda
		vector<string> patterns_to_run;

		if (isAn || isPar) {
			bool isAnagram = isAn;
			string rest;
			if (isAn) rest = (input.substr(0, 8) == "/anagram") ? input.substr(8) : input.substr(4);
			else rest = (input.substr(0, 12) == "/paronomasia") ? input.substr(12) : input.substr(4);

			rest.erase(0, rest.find_first_not_of(" "));

			string word, extra_r = "", tolerance_str = "";
			size_t b_start = rest.find('[');
			size_t last_space = rest.find_last_of(" ");

			if (b_start != string::npos) {
				word = rest.substr(0, b_start);
				size_t b_end = rest.find(']', b_start);
				if (b_end != string::npos) {
					extra_r = rest.substr(b_start + 1, b_end - b_start - 1);
					tolerance_str = rest.substr(b_end + 1);
				}
			}
			else if (last_space != string::npos) {
				string possible_n = rest.substr(last_space + 1);
				string check_n = possible_n;
				if (!check_n.empty() && check_n.back() == '*') check_n.pop_back();

				if (!check_n.empty() && all_of(check_n.begin(), check_n.end(), [](unsigned char c) { return isdigit(c); })) {
					word = rest.substr(0, last_space);
					tolerance_str = possible_n;
				}
				else word = rest;
			}
			else word = rest;

			word.erase(remove(word.begin(), word.end(), ' '), word.end());
			string normWord = normalizeWord(word);

			if (isAnagram) {
				if (!tolerance_str.empty()) {
					string tmp = tolerance_str;
					if (tmp.back() == '*') tmp.pop_back();
					if (all_of(tmp.begin(), tmp.end(), [](unsigned char c) { return isdigit(c); })) {
						tolerance_str = tmp + "*";
					}
				}
				map<char, int> counts;
				for (char c : normWord) counts[c]++;
				string expanded = ". [";
				for (auto const& [ch, count] : counts) expanded += to_string(count) + ch + ",";
				expanded += to_string(normWord.length());
				if (!extra_r.empty()) expanded += "," + extra_r;
				expanded += "] " + tolerance_str;
				inputLine = expanded;
			}
			else {
				string pattern = "";
				int vowels = 0;
				for (char c : normWord) {
					if (isVowel(c)) vowels++;
					else { pattern += c; pattern += "*"; }
				}
				string expanded = pattern + " [" + to_string(vowels) + "V*";
				if (!extra_r.empty()) expanded += "," + extra_r;
				expanded += "] " + tolerance_str;
				inputLine = expanded;
			}
		}

		if (isAns) {
			string rest = (input.substr(0, 12) == "/anasyllabic") ? input.substr(12) : input.substr(4);
			rest.erase(0, rest.find_first_not_of(" "));

			string word, extra_r = "", tolerance_str = "";
			size_t b_start = rest.find('[');
			size_t last_space = rest.find_last_of(" ");

			if (b_start != string::npos) {
				word = rest.substr(0, b_start);
				size_t b_end = rest.find(']', b_start);
				if (b_end != string::npos) {
					extra_r = rest.substr(b_start + 1, b_end - b_start - 1);
					tolerance_str = rest.substr(b_end + 1);
				}
			}
			else if (last_space != string::npos) {
				string possible_n = rest.substr(last_space + 1);
				string check_n = possible_n;
				if (!check_n.empty() && check_n.back() == '*') check_n.pop_back();

				if (!check_n.empty() && all_of(check_n.begin(), check_n.end(), [](unsigned char c) { return isdigit(c); })) {
					word = rest.substr(0, last_space);
					tolerance_str = possible_n;
				}
				else word = rest;
			}
			else word = rest;

			word.erase(remove(word.begin(), word.end(), ' '), word.end());
			string normWord = normalizeWord(word);

			vector<string> syllables = getSyllables(normWord);
			sort(syllables.begin(), syllables.end());

			// Generar todas las permutaciones posibles de las sílabas
			do {
				string p = "";
				for (const string& s : syllables) p += s;
				if (!extra_r.empty()) p += " [" + extra_r + "]";
				if (!tolerance_str.empty()) p += " " + tolerance_str;
				patterns_to_run.push_back(p);
			} while (next_permutation(syllables.begin(), syllables.end()));

			cout << "(Buscando en " << patterns_to_run.size() << " permutaciones silábicas...)\n";
		}

		if (isAnp || isEpi || isMul || isUni) {
			string rest;
			if (isAnp) rest = (input.substr(0, 9) == "/anaphora") ? input.substr(9) : input.substr(4);
			else if (isEpi) rest = (input.substr(0, 9) == "/epiphora") ? input.substr(9) : input.substr(4);
			else if (isMul) rest = (input.substr(0, 14) == "/multisyllabic") ? input.substr(14) : input.substr(4);
			else if (isUni) rest = (input.substr(0, 12) == "/univocalism") ? input.substr(12) : input.substr(4);

			rest.erase(0, rest.find_first_not_of(" "));

			string word, extra_r = "", tolerance_str = "";
			size_t b_start = rest.find('[');
			size_t last_space = rest.find_last_of(" ");

			if (b_start != string::npos) {
				word = rest.substr(0, b_start);
				size_t b_end = rest.find(']', b_start);
				if (b_end != string::npos) {
					extra_r = rest.substr(b_start + 1, b_end - b_start - 1);
					tolerance_str = rest.substr(b_end + 1);
				}
			}
			else if (last_space != string::npos) {
				string possible_n = rest.substr(last_space + 1);
				string check_n = possible_n;
				if (!check_n.empty() && check_n.back() == '*') check_n.pop_back();

				if (!check_n.empty() && all_of(check_n.begin(), check_n.end(), [](unsigned char c) { return isdigit(c); })) {
					word = rest.substr(0, last_space);
					tolerance_str = possible_n;
				}
				else word = rest;
			}
			else word = rest;

			word.erase(remove(word.begin(), word.end(), ' '), word.end());
			string normWord = normalizeWord(word);

			if (isAnp) {
				string expanded = normWord + ".";
				if (!extra_r.empty()) expanded += " [" + extra_r + "]";
				if (!tolerance_str.empty()) expanded += " " + tolerance_str;
				inputLine = expanded;
			}
			else if (isEpi) {
				string expanded = "." + normWord;
				if (!extra_r.empty()) expanded += " [" + extra_r + "]";
				if (!tolerance_str.empty()) expanded += " " + tolerance_str;
				inputLine = expanded;
			}
			else if (isMul) {
				string pattern = ".";
				int vowels = 0;
				for (char c : normWord) {
					if (isVowel(c)) {
						pattern += c;
						pattern += ".";
						vowels++;
					}
				}
				string expanded = pattern + " [" + to_string(vowels) + "V*";
				if (!extra_r.empty()) expanded += "," + extra_r;
				expanded += "]";
				if (!tolerance_str.empty()) expanded += " " + tolerance_str;
				inputLine = expanded;
			}
			else if (isUni) {
				char v = '\0';
				for (char c : normWord) {
					if (isVowel(c)) { v = c; break; }
				}
				if (v != '\0') {
					string expanded = ". [" + string(1, v);
					string all_v = "AEIOU";
					for (char c : all_v) {
						if (c != v) expanded += ",0" + string(1, c);
					}
					if (!extra_r.empty()) expanded += "," + extra_r;
					expanded += "]";
					if (!tolerance_str.empty()) expanded += " " + tolerance_str;
					inputLine = expanded;
				}
			}
		}

		if (input == "/help" || input == "/hp") {
			// (Mismo código de ayuda)
			cout << "\n--- BUSCADOR DE PALABRAS ---\n";
			cout << "Busca palabras en un diccionario usando un lenguaje de patrones.\n\n";
			cout << "Una busqueda se define en una sola linea y puede tener hasta 3 partes:\n";
			cout << "  1) Estructura de la palabra (obligatoria)\n";
			cout << "  2) Restricciones globales entre [] (opcional)\n";
			cout << "  3) Tolerancia a errores, numero al final (opcional)\n\n";
			cout << "Cada parte es independiente y puede combinarse libremente.\n\n";
			cout << "Usa /pattern, /restriction y /tolerance para mas detalles.\n\n";
			cout << "Usa /commands (o /cmd) para ver todos los comandos\n";
			continue;
		}

		if (input == "/pattern" || input == "/pat") {
			// (Mismo código de /pattern)
			continue;
		}

		if (input == "/restriction" || input == "/res") {
			// (Mismo código de /restriction)
			continue;
		}

		if (input == "/tolerance" || input == "/tol") {
			// (Mismo código de /tolerance)
			continue;
		}

		if ((input.size() >= 5 && input.substr(0, 5) == "/load") || (input.size() >= 3 && input.substr(0, 3) == "/ld")) {
			string rest = (input.substr(0, 5) == "/load") ? input.substr(5) : input.substr(3);
			rest.erase(0, rest.find_first_not_of(" \t"));
			if (rest.empty()) listDictionaries();
			else if (loadDictionary(rest, raw_dict, dictionary)) currentDict = rest;
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
							wp_n = stoi(possible_n);
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
					wp_n = stoi(check_n);
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

				parseInput(pLine, elems, resources, tolerance, is_total);

				for (size_t i = 0; i < dictionary.size(); ++i) {
					if (matched_words[i]) continue; // Ya fue encontrada en otra permutación

					const string& w = dictionary[i];
					if (w.length() >= 100) continue;

					int res_errors = checkResources(w, resources);
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
	return 0;
}