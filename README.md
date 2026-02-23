# 🔍 Buscador de Palabras por Patrones

Este proyecto implementa un **buscador de palabras** que trabaja sobre un **diccionario local** (ampliable por el usuario) y devuelve todas las palabras que cumplen un **patrón definido por ti**.

El patrón puede componerse de **hasta 3 partes**:

1. 🧩 **Estructura de la palabra**
2. 📊 **Restricciones adicionales** (opcional)
3. 🎯 **Tolerancia a errores** (opcional)

---

## 🧩 1. Estructura de la palabra

Define cómo debe ser la palabra letra a letra.

### 📌 Sintaxis

| Símbolo | Significado |
|-------|------------|
| `*` | Exactamente **1 letra** |
| `(n,m)` | Entre **n y m letras cualquiera** |
| `(n,m,V)` | Entre **n y m vocales** |
| `(n,m,C)` | Entre **n y m consonantes** |
| `X` | La **letra X exacta** |
| `.` | **Cualquier cantidad de letras** |

📌 Si `n` está vacío → se asume `0`  
📌 Si `m` está vacío → se asume **infinito**

Ejemplos válidos:
- `(,5,C)` → de 0 a 5 consonantes  
- `(,,V)` → cualquier número de vocales  

---

### 🧪 Ejemplo de estructura


(1,3,C)E(1,3)A.


**Significado:**

- 1 letra cualquiera  
- 1–3 consonantes  
- La letra `E`  
- 1–3 letras cualquiera  
- La letra `A`  
- Cualquier cantidad de letras  

✔️ **CRETINAS** cumple este patrón.

---

## 📊 2. Restricciones (opcional)

Permiten imponer condiciones sobre **la palabra completa**, independientemente de la estructura.

### 📌 Sintaxis general


[nX, >nX, <nX, >=nX, <=nX]


Donde `X` puede ser:
- Una letra concreta (`A`, `E`, `K`, etc.)
- `V*` → vocales
- `C*` → consonantes

Forma corta:
- `X` → equivalente a `>=1X`

---

### 🧪 Ejemplo de restricciones


[5V*, >2O, E, 1P, 0K]


**Significado:**

- `5V*` → exactamente 5 vocales  
- `>2O` → más de 2 letras `O`  
- `E` → al menos una `E`  
- `1P` → exactamente una `P`  
- `0K` → ninguna `K`  

---

## 🎯 3. Tolerancia (opcional)

Define cuántos errores se permiten al comparar la palabra con el patrón.


2


➡️ La palabra puede **fallar el patrón en hasta 2 letras** y aun así mostrarse.

📌 Por defecto, la tolerancia es `0`.

---

## 🧪 Ejemplo completo


(1,3,C)E(1,3)A. [5V, >2O, E, 1P, 0K] 2
