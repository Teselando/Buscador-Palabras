#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <windows.h>

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

// Memoización estática para evitar reservar memoria 650k veces
// word_len(100) x pattern_elems(50) x tolerance(11)
int memo_buffer[100][50][11];

// --- UTILIDADES DE TEXTO (Normalización) ---

string replaceAll(string str, const string& from, const string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
	return str;
}

string normalizeWord(const string& w) {
	string res;
	res.reserve(w.size());

	for (size_t i = 0; i < w.size(); ) {
		unsigned char c = w[i];

		// ASCII
		if (c < 128) {
			if (c >= 'a' && c <= 'z') res += (char)toupper(c);
			else if (c >= 'A' && c <= 'Z') res += c;
			i++;
			continue;
		}

		// UTF-8 español (todos empiezan por 0xC3)
		if (c == 0xC3 && i + 1 < w.size()) {
			unsigned char d = w[i + 1];

			switch (d) {
				// Á É Í Ó Ú Ü
			case 0x81: case 0xA1: res += 'A'; break;
			case 0x89: case 0xA9: res += 'E'; break;
			case 0x8D: case 0xAD: res += 'I'; break;
			case 0x93: case 0xB3: res += 'O'; break;
			case 0x9A: case 0xBA: res += 'U'; break;
			case 0x9C: case 0xBC: res += 'U'; break;

				// Ñ
			case 0x91: case 0xB1: res += '~'; break;

			default:
				break;
			}
			i += 2;
			continue;
		}

		// Cualquier otro byte raro → ignorado
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

// --- SISTEMA DE CACHÉ BINARIA (Carga Ultrarrápida) ---

void saveBinaryCache(const string& filename, const vector<string>& raw, const vector<string>& norm) {
	ofstream out(filename, ios::binary);
	size_t size = raw.size();
	out.write((char*)&size, sizeof(size));
	for (size_t i = 0; i < size; ++i) {
		size_t r_len = raw[i].size();
		size_t n_len = norm[i].size();
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
	raw.resize(size);
	norm.resize(size);
	for (size_t i = 0; i < size; ++i) {
		size_t r_len, n_len;
		in.read((char*)&r_len, sizeof(r_len));
		raw[i].resize(r_len);
		in.read(&raw[i][0], r_len);
		in.read((char*)&n_len, sizeof(n_len));
		norm[i].resize(n_len);
		in.read(&norm[i][0], n_len);
	}
	return true;
}

// --- LÓGICA DE BÚSQUEDA ---

void parseInput(string input, vector<PatternElement>& elems, vector<ResourceCondition>& resources, int& tolerance) {
	input.erase(0, input.find_first_not_of(" "));
	input.erase(input.find_last_not_of(" ") + 1);

	string p_str = input, r_str = "";
	tolerance = 0;

	size_t b_start = input.find('[');
	if (b_start != string::npos) {
		size_t b_end = input.find(']', b_start);
		p_str = input.substr(0, b_start);
		r_str = input.substr(b_start + 1, b_end - b_start - 1);
		string rem = input.substr(b_end + 1);
		rem.erase(0, rem.find_first_not_of(" "));
		if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char ch) { return isdigit(ch); }))
			tolerance = stoi(rem);
	}
	else {
		size_t last_space = input.find_last_of(" ");
		if (last_space != string::npos) {
			string rem = input.substr(last_space + 1);
			if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char ch) { return isdigit(ch); })) {
				tolerance = stoi(rem);
				p_str = input.substr(0, last_space);
			}
		}
	}

	p_str.erase(p_str.find_last_not_of(" ") + 1);

	for (size_t i = 0; i < p_str.length(); ++i) {
		char c = p_str[i];
		if (c == '*') elems.push_back({ ANY, 1, 1, 0 });
		else if (c == '.') elems.push_back({ ANY, 0, 99, 0 });
		else if (c == '(') {
			size_t j = p_str.find(')', i);
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
			for (auto& c_tgt : target) c_tgt = (char)toupper((unsigned char)c_tgt);
			int num = 1;
			if (op.empty() && num_str.empty()) { op = ">="; num = 1; }
			else if (op.empty()) { op = "=="; num = stoi(num_str); }
			else if (!num_str.empty()) num = stoi(num_str);
			resources.push_back({ op, num, target });
		}
	}
}

bool checkResources(const string& word, const vector<ResourceCondition>& resources) {
	if (resources.empty()) return true;
	int v_count = 0, c_count = 0;
	int l_count[256] = { 0 };
	for (char c : word) {
		if (isVowel(c)) v_count++;
		if (isConsonant(c)) c_count++;
		l_count[(unsigned char)c]++;
	}
	for (const auto& res : resources) {
		int val = 0;
		if (res.target == "V*") val = v_count;
		else if (res.target == "C*") val = c_count;
		else val = l_count[(unsigned char)res.target[0]];

		if (res.op == "==" && val != res.num) return false;
		if (res.op == ">=" && val < res.num) return false;
		if (res.op == "<=" && val > res.num) return false;
		if (res.op == ">" && val <= res.num) return false;
		if (res.op == "<" && val >= res.num) return false;
	}
	return true;
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
	// Configuración para tildes en consola
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	ios_base::sync_with_stdio(false); cin.tie(NULL);

	vector<string> dictionary, raw_dict;
	string binFile = "diccionario.bin";

	cout << "Cargando... ";
	if (!loadBinaryCache(binFile, raw_dict, dictionary)) {
		cout << "(Indexando .txt por primera vez)... ";
		ifstream file("diccionario.txt");
		if (!file) { cerr << "Error: No existe diccionario.txt" << endl; return 1; }
		string line;
		while (getline(file, line)) {
			if (!line.empty()) {
				raw_dict.push_back(line);
				dictionary.push_back(normalizeWord(line));
			}
		}
		saveBinaryCache(binFile, raw_dict, dictionary);
	}
	cout << "[OK] " << dictionary.size() << " palabras." << endl;

	while (true) {
		cout << "\nPatron: ";
		string input; getline(cin, input);
		if (input == "exit" || input.empty()) break;

		vector<PatternElement> elems;
		vector<ResourceCondition> resources;
		int tolerance = 0;
		parseInput(input, elems, resources, tolerance);

		int count = 0;
		for (size_t i = 0; i < dictionary.size(); ++i) {
			const string& w = dictionary[i];
			if (w.length() >= 100 || !checkResources(w, resources)) continue;

			// Reset del buffer para los rangos que usaremos
			for (int r = 0; r <= (int)w.length(); ++r)
				for (int e = 0; e <= (int)elems.size(); ++e)
					for (int t = 0; t <= tolerance; ++t) memo_buffer[r][e][t] = -1;

			if (matchPattern(w, 0, elems, 0, tolerance)) {
				cout << "- " << raw_dict[i] << "\n";
				count++;
			}
		}
		cout << "Total: " << count << endl;
	}
	return 0;
}