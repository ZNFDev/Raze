# Raze Language

A fast, C/Java-style scripting language designed for game modding, engine scripting, and general-purpose use.
Call native functions at raw runtime addresses — zero overhead, no bindings required.


---

⚡ Versions Overview

Raze v1.x → Lightweight core, structs + native calls

Raze v2.0 → Full-featured language (OOP, stdlib, closures, maps, etc.)



---

🚀 Build

# v1
cd raze
make
make test
make run-repl

# v2
cd raze2
make
make test
./raze tests/import_test.rz

Requirements:

g++ with C++17


sudo apt install g++


---

✨ Core Features

🔥 Native Runtime Calls (Core Feature)

Call functions at raw memory addresses

No wrappers, no bindings

Designed for game hacking / modding


native explode = (float, float, float, float) -> void @ 0x7F001234;
explode(128.0, 64.0, 256.0, 15.0);


---

🧱 Language Features

Types

int, float, bool, string, void

Arrays (dynamic, heterogeneous)

Structs (v1)

Classes (v2)


Control Flow

if / else

for, while

break, continue


Operators

Arithmetic: + - * / %

Bitwise: & | ^ ~ << >>

Logical: && || !



---

🧩 Raze v1 — Core Language

Structs

struct Vec3 {
    float x;
    float y;
    float z;
}

Functions

func distance(Vec3 a, Vec3 b) -> float {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrt(dx*dx + dy*dy + dz*dz);
}

Native Calls

native get_hp = (int) -> int @ 0x7F5566AABB;
int hp = get_hp(42);


---

🚀 Raze v2.0 — Advanced Features

🧠 Type Inference

auto x = 42;
var  y = 3.14;
let  name = "Raze";
val  flag = true;


---

🏗️ Classes & Inheritance

class Entity {
    string name;

    func init(string n) {
        this.name = n;
    }
}

class Player extends Entity {
    func init(string n) {
        super.init(n);
    }
}


---

⚙️ First-Class Functions / Closures

func makeCounter(int start) -> var {
    var n = start;
    return func() -> int { n++; return n; };
}


---

🗺️ Maps

var m = {"name": "BlockMan", "level": 42};
println(m["name"]);


---

📦 Arrays (Functional API)

var nums = [1,2,3];

nums.map(func(n,i){ return n*2; });
nums.filter(func(n,i){ return n%2==0; });
nums.reduce(func(acc,n,i){ return acc+n; }, 0);


---

🔤 String Utilities

"hello".upper();
"a,b,c".split(",");
"{}+{}={}".format(1,2,3);


---

🔁 For-In Loop

for (auto x in [1,2,3]) { print(str(x)); }


---

⚠️ Exception Handling

try {
    throw "error!";
} catch (e) {
    println(e);
}


---

📥 Import System

import "stdlib/math.rz";


---

📚 Built-in Functions

Math

sqrt, pow, sin, cos, tan, log, exp, min, max

Conversion

int(), float(), str(), bool(), hex(), bin()

Collections

array(), map(), range(), push(), pop(), len()

I/O

print(), println(), input()

System

time(), sleep(), rand(), exit()


---

🧠 Native Integration (C++)

Register Function

interp.registerNative("explode", [](std::vector<ValPtr> args) {
    float x = args[0]->toFloat();
    return Value::null();
});

Register Raw Address

interp.registerAddr(
    "native_add",
    reinterpret_cast<uintptr_t>(my_fn_ptr),
    "int",
    {"int", "int"}
);


---

⚙️ ABI Notes

✅ Integer/pointer args → direct calls work everywhere

⚠️ Float args → use registerNative()

⚠️ Mixed types → requires libffi (-lffi)



---

📦 Standard Library (v2)

Module	Features

math.rz	primes, gcd, fibonacci
string.rz	advanced string utils
collections.rz	Stack, Queue, Set



---

▶️ Running

./raze script.rz
./raze -e 'println(42);'
./raze
