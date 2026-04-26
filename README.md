# ⚡ Raze Language (v3.0)

Raze is a fast, C/Java-style scripting language with Python-level expressiveness, built for **game modding**, engine scripting, and high-performance automation.

The core philosophy of Raze is **zero-overhead interoperability** — directly calling native functions at raw memory addresses without wrappers, bindings, or runtime overhead.

---

## 🚀 What's New in v3

Raze v3 significantly expands language power and developer ergonomics:

- ✨ String interpolation: `"Hello ${name}!"`
- 📝 Multi-line backtick strings
- 🧩 Enums with `::` access
- 🔀 `switch` with multi-value cases
- 🎯 Powerful `match` with pattern guards
- ➗ `**` exponentiation operator
- 🔢 Numeric literals: `0b`, `0o`, `1_000`
- 🔍 `is` and `in` operators
- 🛡️ Optional chaining: `?.` and `?[ ]`
- 📦 Spread operator `...`
- ⚙️ Default + variadic function parameters
- 🔒 `const` variables
- 🏛️ Static class methods `Class::method()`
- 🧠 Interfaces (duck typing)
- 🔗 Proper multi-level `super` dispatch
- 📄 JSON support (`toJson`, `parseJson`)
- 🔎 Regex support (`match`, `find`, `replace`)
- 💻 `exec()` for shell command output
- 📚 Massive stdlib:
  - 30+ array methods
  - 10+ map methods
  - 25+ string utilities
- 📦 Module system with caching

---

## 🔥 Core Features

- **Native Raw Calls:** Call functions at memory addresses directly
- **Modern Syntax:** C/Java structure with Python-like flexibility
- **Object-Oriented:** Classes, inheritance, constructors, `super`
- **Functional Programming:** Lambdas, closures, `map/filter/reduce`
- **Low-Level Friendly:** Bitwise ops, hex literals, memory control
- **Rich Standard Library**

---

## 🛠️ Build & Run

### Requirements
- `g++` (C++17)
- `make`

### Build
```bash
git clone https://github.com/ZNFDev/Raze.git
cd raze
make
make test
make test-import
```
### Usage
```
./raze script.rz              # Run script
./raze -e 'println("hi");'    # Inline execution
./raze                        # REPL
```

---

### 💎 Native Address Calls (The Real Power)
```
native explode = (float, float, float, float) -> void @ 0x7F001234;
native getHP   = (int) -> int @ 0x7F005678;

func main() {
    explode(128.0, 64.0, 256.0, 15.0);
    println("HP: " + str(getHP(42)));
}
```
Directly invoke internal game functions — no bindings, no wrappers.


---

### 🧠 Language Overview

Classes & OOP
```
class Player extends Entity {
    string weapon;

    func init(string name, float hp, string weapon) {
        super.init(name, hp);
        this.weapon = weapon;
    }

    func describe() -> string {
        return super.describe() + " wielding " + this.weapon;
    }
}
```

---

### Functional Programming
```
var nums = [1, 2, 3, 4, 5];

auto evens = nums.filter(func(n, i) => n % 2 == 0);
auto sum   = nums.reduce(func(acc, n, i) => acc + n, 0);
```

---

### Pattern Matching
```
match (value) {
    1 | 2 => println("Low"),
    3 => println("Mid"),
    _ when value > 3 => println("High")
}
```

---

### Error Handling
```
try {
    var data = readFile("config.json");
} catch (err) {
    println("Error: " + err);
}
```

---

### 📚 Standard Library

Module	Features

Core	print, len, typeof, range, casting
Math	sin, cos, sqrt, random, clamp
I/O	readFile, writeFile, listDir
String	split, trim, regex, format
System	time, sleep, exec, getenv
Collections	Stack, Queue, Set, Map
Algo/Vec/Event	Advanced utilities



---

### ⚖️ Native / ABI Notes

✅ Works natively on 64-bit systems

⚠️ Float args may require register-aware binding

🔧 Advanced interop supported via libffi



---

### 📖 Documentation

Full language reference available in:

docs/REFERENCE.md


---

### 📜 License

MIT License — free to use, modify, and distribute.