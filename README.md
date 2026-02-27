# 🔍 Buscador de Palabras por Patrones
## 1.3 Logic & Nested

Este proyecto implementa un **buscador avanzado de palabras** sobre un **diccionario local**
(ampliable por el usuario), usando un **lenguaje expresivo de patrones** diseñado para
exploración lingüística, juegos de palabras, poesía, análisis fonético y experimentación creativa.

El sistema permite:

- Definir estructuras internas de palabras
- Imponer restricciones globales (sílabas, vocales, acento, letras…)
- Permitir errores controlados
- Usar comandos lingüísticos avanzados
- Combinar búsquedas con lógica booleana
- Encadenar búsquedas mediante consultas anidadas
- Seleccionar resultados aleatoriamente

---

## 🧱 Concepto básico de una búsqueda

Una búsqueda se escribe en una sola línea y puede tener hasta tres partes, en este orden:

ESTRUCTURA [RESTRICCIONES] TOLERANCIA

- Estructura → describe la forma interna de la palabra
- Restricciones → filtros globales (opcional)
- Tolerancia → errores permitidos (opcional)

Cada parte es independiente y combinable libremente.

---

## 🧩 1. Estructura de la palabra

Describe cómo debe ser la palabra de izquierda a derecha.
El patrón debe cubrir toda la palabra.

### Elementos básicos

*   *   → exactamente 1 letra cualquiera
*   X   → la letra X exacta (A, B, ~, etc.)
*   .   → cualquier cantidad de letras (incluido cero)

---

### Rangos (entre paréntesis)

(n,m)
(n,m,V)
(n,m,C)

- (n,m)    → entre n y m letras cualquiera
- (n,m,V)  → entre n y m vocales
- (n,m,C)  → entre n y m consonantes

Si n está vacío → se asume 0  
Si m está vacío → se asume infinito  

Ejemplos válidos:
- (,5,C) → de 0 a 5 consonantes
- (,,V) → cualquier número de vocales

---

### Ejemplo de estructura

(1,3,C)E(1,3)A.

Significado:
- 1–3 consonantes
- La letra E
- 1–3 letras cualquiera
- La letra A
- Cualquier cantidad de letras

---

## 📊 2. Restricciones (opcional)

Las restricciones se escriben entre corchetes [] y se aplican a toda la palabra,
independientemente de la estructura.

PATRON [R1,R2,...]

### Forma general de una restricción

[operador][número][objetivo]

Operadores disponibles:
==   >=   <=   >   <

Si se omite el operador:
- X  equivale a >=1X
- 3A equivale a ==3A

### Objetivos posibles

- V*   → número total de vocales
- C*   → número total de consonantes
- S*   → número de sílabas
- T*   → posición de la sílaba tónica (1 = aguda, 2 = llana…)
- A–Z  → ocurrencias de una letra concreta
- (vacío) → longitud total de la palabra
- AB, TR, etc. → ocurrencias de una subcadena

### Ejemplo de restricciones

[5V*, >2O, E, 1P, 0K]

Significado:
- Exactamente 5 vocales
- Más de 2 letras O
- Al menos una E
- Exactamente una P
- Ninguna K

---

## 🎯 3. Tolerancia a errores (opcional)

La tolerancia es un número al final del patrón:

n
n*

### Tolerancia parcial (n)

- Solo afecta al patrón
- Las restricciones deben cumplirse exactamente

### Tolerancia total (n*)

- El asterisco hace que la tolerancia sea global
- Los errores en restricciones también consumen del límite

Ejemplos:
HOLA 1
. [3A] 1*

---

## 🧪 Ejemplo completo

(1,3,C)E(1,3)A. [5V*, >2O, E, 1P, 0K] 2

---

## 🛠️ Comandos especiales

Todos los comandos aceptan restricciones, tolerancia y consultas anidadas.

### 🎲 /random (/rd)

Devuelve resultados aleatorios de una búsqueda.

 /rd n PATRON

Ejemplo:
 /rd 5 . [>=3V*]

---

### 🔪 /calembour (/cal)

Divide una palabra en segmentos que estén en el diccionario.

 /cal PALABRA [restricciones] n

---

### 🔄 /anagram (/ang)

Busca anagramas de una palabra.

 /ang PALABRA

---

### 🔊 /paronomasia (/par)

Busca palabras con el mismo esqueleto consonántico.

---

### 🔀 /anasyllabic (/ans)

Reordena las sílabas de una palabra y busca coincidencias.

---

### 🔁 /anaphora (/anp) y /epiphora (/epi)

- /anp → palabras que empiezan por…
- /epi → palabras que terminan en…

---

### 🎼 /assonant (/aso) y /consonant (/con)

- Rima asonante (solo vocales)
- Rima consonante (sufijo exacto desde la tónica)

---

### 🧠 /multisyllabic (/mul)

Busca palabras con la misma estructura vocálica.

---

### 🔤 /univocalism (/uni)

Busca palabras que usen una sola vocal.

---

### 🎭 /wordplay (/wp)

Incrementa automáticamente la tolerancia hasta encontrar resultados nuevos.

---

## 🧮 Lógica booleana

(A) && (B)   → intersección  
(A) || (B)   → unión  
(A) - (B)    → diferencia  
!(A)         → complemento  

Precedencia:
! > && > - > ||

---

## 🪆 Consultas anidadas

Una consulta puede contener otra consulta entre paréntesis, que se evalúa primero.

 /cal (/rd 3 [E])
 /aso ((/cal SOL) || (/cal LUNA))

Las consultas internas pueden ser:
- patrones normales
- comandos
- expresiones booleanas
- /random

---

## 📚 Diccionarios

- Los diccionarios son archivos .txt
- Se cachean automáticamente en .bin
- Se gestionan con /load (/ld)

---

## 🚪 Comandos generales

/help        → ayuda general  
/commands    → lista completa de comandos  
/pattern     → guía de estructuras  
/restriction → guía de restricciones  
/tolerance   → guía de tolerancia  
/load        → cambiar diccionario  
/exit        → salir  
