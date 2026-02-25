#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <windows.h>
#include <filesystem>

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

void parseInput(string input, vector<PatternElement>& elems, vector<ResourceCondition>& resources, int& tolerance) {
	input.erase(0, input.find_first_not_of(" "));
	input.erase(input.find_last_not_of(" ") + 1);

	string p_str = input, r_str = "";
	tolerance = 0;

	size_t b_start = input.find('[');
	if (b_start != string::npos) {
		size_t b_end = input.find(']', b_start);
		if (b_end != string::npos) {
			p_str = input.substr(0, b_start);
			r_str = input.substr(b_start + 1, b_end - b_start - 1);
			string rem = input.substr(b_end + 1);
			rem.erase(0, rem.find_first_not_of(" "));
			if (!rem.empty() && all_of(rem.begin(), rem.end(), [](unsigned char ch) { return isdigit(ch); }))
				tolerance = stoi(rem);
		}
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

bool checkResources(const string& word, const vector<ResourceCondition>& resources) {
	if (resources.empty()) return true;
	int v_count = 0, c_count = 0, l_count[256] = { 0 };
	for (char c : word) {
		if (isVowel(c)) v_count++;
		if (isConsonant(c)) c_count++;
		l_count[(unsigned char)c]++;
	}
	for (const auto& res : resources) {
		int val = 0;
		if (res.target == "V*") val = v_count;
		else if (res.target == "C*") val = c_count;
		else if (res.target == "") val = (int)word.length();
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
	SetConsoleOutputCP(65001); // UTF-8
	SetConsoleCP(65001);
	ios_base::sync_with_stdio(false); cin.tie(NULL);

	vector<string> dictionary, raw_dict;
	string currentDict = "default";

	loadDictionary(currentDict, raw_dict, dictionary);

	cout << "\nDiccionario actual: " << currentDict << endl <<endl;
	cout << "Escribe /help para obtener ayuda." << endl;

	while (true) {
		// "Patrón" con escape hexadecimal para evitar errores de codificación
		cout << "\nPatr\xC3\xB3n:\n\n";
		string inputLine; getline(cin, inputLine);

		string input = inputLine;
		input.erase(0, input.find_first_not_of(" \t\r\n"));
		size_t last = input.find_last_not_of(" \t\r\n");
		if (last != string::npos) input.erase(last + 1);

		if (input == "/exit") break;
		if (input.empty()) continue;

		if (input == "/commands") {
			cout << "\n--- LISTA DE COMANDOS ---" << endl;
			cout << "/help         -> Explicacion general del buscador." << endl;
			cout << "/pattern      -> Como definir la estructura (comodines y rangos)." << endl;
			cout << "/restriction  -> Como usar filtros entre corchetes []." << endl;
			cout << "/tolerance    -> Como permitir errores en la busqueda." << endl;
			cout << "/load         -> Muestra o cambia el diccionario actual." << endl;
			cout << "/exit         -> Cierra la aplicacion." << endl;
			continue;
		}

		if (input == "/help") {
			cout << "\n--- BUSCADOR DE PALABRAS ---\n";
			cout << "Busca palabras en un diccionario usando un lenguaje de patrones.\n\n";
			cout << "Una busqueda se define en una sola linea y puede tener hasta 3 partes:\n";
			cout << "  1) Estructura de la palabra (obligatoria)\n";
			cout << "  2) Restricciones globales entre [] (opcional)\n";
			cout << "  3) Tolerancia a errores, numero al final (opcional)\n\n";
			cout << "Cada parte es independiente y puede combinarse libremente.\n\n";
			cout << "Usa /pattern, /restriction y /tolerance para mas detalles.\n\n";
			cout << "Usa /commands para ver todos los comandos\n";
			continue;
		}

		if (input == "/pattern") {
			cout << "\n--- 1. ESTRUCTURA DE LA PALABRA ---\n";
			cout << "Describe la forma interna de la palabra, de izquierda a derecha.\n";
			cout << "El patron debe cubrir toda la palabra.\n\n";

			cout << "ELEMENTOS DISPONIBLES:\n";
			cout << "  *        -> exactamente 1 letra cualquiera\n";
			cout << "  X        -> la letra X exacta\n";
			cout << "  .        -> cualquier cantidad de letras\n\n";

			cout << "RANGOS:\n";
			cout << "  (n,m)    -> entre n y m letras cualquiera\n";
			cout << "  (n,m,V)  -> entre n y m vocales\n";
			cout << "  (n,m,C)  -> entre n y m consonantes\n\n";

			cout << "NOTAS:\n";
			cout << "  - Si n esta vacio, se asume 0\n";
			cout << "  - Si m esta vacio, se asume infinito\n";
			cout << "  - Cada elemento consume letras consecutivas\n\n";

			cout << "EJEMPLO:\n";
			cout << "  (1,2,C)A.\n";
			cout << "  -> 1 o 2 consonantes, luego una A y luego cualquier cosa\n";
			continue;
		}

		if (input == "/restriction") {
			cout << "\n--- 2. RESTRICCIONES ---\n";
			cout << "Las restricciones son condiciones sobre la palabra completa.\n";
			cout << "Se escriben entre corchetes [] y pueden combinarse con comas.\n\n";

			cout << "FORMATO GENERAL:\n";
			cout << "  nX       -> exactamente n veces X\n";
			cout << "  >=nX     -> al menos n veces X\n";
			cout << "  <=nX     -> como maximo n veces X\n";
			cout << "  >nX      -> mas de n veces X\n";
			cout << "  <nX      -> menos de n veces X\n";
			cout << "  X        -> forma corta de >=1X\n\n";

			cout << "X PUEDE SER:\n";
			cout << "  - Una letra concreta (A, B, C, ...)\n";
			cout << "  - V*  -> vocales\n";
			cout << "  - C*  -> consonantes\n\n";

			cout << "LONGITUD DE LA PALABRA:\n";
			cout << "  n        -> longitud exacta n\n";
			cout << "  >=n      -> longitud minima n\n";
			cout << "  <=n      -> longitud maxima n\n\n";

			cout << "EJEMPLO:\n";
			cout << "  [>=2V*,0K]\n";
			cout << "  -> al menos 2 vocales y ninguna letra K\n";
			continue;
		}

		if (input == "/tolerance") {
			cout << "\n--- 3. TOLERANCIA ---\n";
			cout << "Numero al final del patron que indica cuantos errores se permiten.\n\n";
			cout << "La tolerancia permite aceptar coincidencias aproximadas.\n";
			cout << "Cada desviacion del patron consume parte de la tolerancia.\n\n";

			cout << "Se consideran errores, por ejemplo:\n";
			cout << "  - Letras que no cumplen el tipo esperado\n";
			cout << "  - Letras que sobran\n";
			cout << "  - Letras que faltan para cumplir un rango\n\n";

			cout << "Si el total de errores no supera la tolerancia, la palabra se acepta.\n";
			cout << "Por defecto, la tolerancia es 0.\n\n";

			cout << "EJEMPLO:\n";
			cout << "  HOLA 1\n";
			cout << "  -> permite una letra incorrecta respecto al patron (BOLA, OLA, HOLI)\n";
			continue;
		}

		if (input.size() >= 5 && input.substr(0, 5) == "/load") {
			string rest = input.substr(5);
			rest.erase(0, rest.find_first_not_of(" \t"));
			if (rest.empty()) listDictionaries();
			else if (loadDictionary(rest, raw_dict, dictionary)) currentDict = rest;
			continue;
		}

		// Lógica de búsqueda
		vector<PatternElement> elems;
		vector<ResourceCondition> resources;
		int tolerance = 0;
		parseInput(inputLine, elems, resources, tolerance);

		int count = 0;
		for (size_t i = 0; i < dictionary.size(); ++i) {
			const string& w = dictionary[i];
			if (w.length() >= 100 || !checkResources(w, resources)) continue;
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