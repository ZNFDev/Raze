# Raze Language v3.0 — Complete Reference

Raze is a fast, C/Java-style scripting language with Python-level expressiveness.
Key features: **native function calls at raw memory addresses**, first-class functions,
classes with inheritance, enums, pattern matching, string interpolation, and a rich stdlib.

---

## Build & Run

```bash
cd raze3
make              # builds ./raze
./raze script.rz  # run file
./raze -e 'println("hello!");'  # inline eval
./raze            # interactive REPL
make test         # run tests/test.rz
```

Requires: `g++` with C++17.

---

## Variables

```raze
auto  x   = 42;         // type inferred (int)
val   pi  = 3.14159;    // same as auto
var   msg = "hello";    // same as auto
let   ok  = true;       // same as auto
const MAX = 100;        // immutable — cannot reassign

// Explicit types
int    count = 0;
float  speed = 9.8;
bool   alive = true;
string name  = "Raze";
```

---

## Types

| Type       | Example               | Notes                  |
|------------|-----------------------|------------------------|
| `int`      | `42`, `0xFF`, `0b101`, `0o17`, `1_000` | 64-bit signed |
| `float`    | `3.14`, `1.5e10`      | 64-bit double          |
| `bool`     | `true`, `false`       |                        |
| `string`   | `"hello"`, backtick   |                        |
| `var`      | any                   | dynamic / inferred     |
| arrays     | `[1, 2, 3]`           | dynamic, mixed types   |
| maps       | `{"k": v}`            | ordered key-value      |
| classes    | `new Player(...)`     | OOP instances          |
| enums      | `Direction::NORTH`    |                        |
| functions  | `func(int x)->int{}`  | first-class values     |
| `null`     |                       | absence of value       |

---

## Literals

```raze
// Numbers
42        1_000_000    // decimal (underscores ok)
0xFF      0xDEADBEEF  // hex
0b1010    0b1111_0000 // binary
0o755                 // octal
3.14      2.5e10      // float

// Strings
"Hello, world!"
"Escape: \n \t \\ \""
"Interpolated: ${name}, age ${user.age + 1}"
`Backtick multi-line
  raw string, no escaping needed
  except \` for backtick itself`

// Arrays
[1, 2, 3]
["a", "b", ...otherArr]   // spread

// Maps
{"name": "Alice", "age": 30}
{}                         // empty map
```

---

## Operators

```raze
// Arithmetic
+  -  *  /  %  **       // ** = exponentiation

// Comparison
==  !=  <  >  <=  >=

// Logical
&&  ||  !

// Bitwise
&  |  ^  ~  <<  >>

// Assignment
=  +=  -=  *=  /=  %=  **=  &=  |=
++  --  (pre and post)

// Special
??   // null-coalescing:  a ?? "default"
?.   // optional chain:   obj?.field
?[   // optional index:   arr?[0]
...  // spread:           fn(...arr)
is   // type check:       x is int
in   // membership:       x in arr
?:   // ternary:          cond ? a : b
```

---

## Control Flow

### if / else

```raze
if (score >= 90) {
    println("A");
} else if (score >= 80) {
    println("B");
} else {
    println("F");
}
```

### while / for

```raze
while (hp > 0) { hp -= damage; }

for (int i = 0; i < 10; i++) { print(i); }

// For-in: arrays, maps, strings
for (auto item in [1, 2, 3]) { println(item); }
for (auto key in myMap)       { println(key + "=" + str(myMap[key])); }
for (auto ch in "Raze")       { print(ch); }
for (auto n in range(0, 10, 2)) { print(n); }  // 0 2 4 6 8
```

### switch

```raze
switch (value) {
    case 1, 2:      println("one or two");
    case "hello":   println("greeting");
    default:        println("other");
}
```

### match (pattern matching)

```raze
match score {
    100              => { println("perfect"); }
    90 | 91 | 92     => { println("excellent"); }
    default when score >= 60 => { println("pass"); }
    default          => { println("fail"); }
}
// 'it' is bound to the matched value inside arm body
```

---

## Functions

```raze
// Basic
func add(int a, int b) -> int { return a + b; }

// No return type (void)
func greet(string name) { println("Hello, ${name}!"); }

// Default parameters
func greet(string name, string prefix = "Hello") -> string {
    return "${prefix}, ${name}!";
}
greet("Alice");           // Hello, Alice!
greet("Bob", "Welcome");  // Welcome, Bob!

// Variadic
func sum(...nums) -> int {
    auto total = 0;
    for (auto n in nums) { total += int(n); }
    return total;
}
sum(1, 2, 3, 4, 5);   // 15
sum(...myArray);        // spread an array

// First-class / lambda
auto double = func(int x) -> int { return x * 2; };
println(double(21));   // 42

// Closure
func makeCounter(int start) -> var {
    var n = start;
    return func() -> int { n++; return n; };
}
auto c = makeCounter(0);
println(c());   // 1
println(c());   // 2

// Higher-order
func map(var arr, var fn) -> var { return arr.map(fn); }
```

---

## Classes

```raze
class Animal {
    string name;
    int    age;

    func init(string n, int a) {
        this.name = n;
        this.age  = a;
    }

    func speak() -> string { return this.name + " makes a sound"; }
    func describe() -> string { return "${this.name} (${this.age}yo)"; }

    static func create(string n) -> Animal {
        return new Animal(n, 0);
    }
}

class Dog extends Animal {
    string breed;

    func init(string n, int a, string b) {
        super.init(n, a);  // call parent constructor
        this.breed = b;
    }

    func speak() -> string { return this.name + " says: Woof!"; }
    func describe() -> string {
        return super.describe() + " breed=" + this.breed;
    }
}

// Instantiation
auto dog = new Dog("Rex", 3, "Labrador");
println(dog.speak());       // Rex says: Woof!
println(dog.describe());    // Rex (3yo) breed=Labrador

// Static method call
auto a = Animal::create("Cat");

// Type checking
println(dog is Dog);      // true
println(dog is Animal);   // false (exact type)
```

### Interfaces (duck typing)

```raze
interface Printable {
    func describe;
    func toString;
}

class Report implements Printable {
    string title;
    func init(string t) { this.title = t; }
    func describe() -> string { return "Report: " + this.title; }
    func toString() -> string { return this.describe(); }
}
```

---

## Enums

```raze
enum Direction { NORTH, SOUTH, EAST, WEST }     // 0,1,2,3
enum Status    { DEAD=0, ALIVE=1, STUNNED=2 }
enum Color     { RED=0xFF0000, GREEN=0x00FF00, BLUE=0x0000FF }

auto dir = Direction::NORTH;     // 0
auto col = Color::RED;           // 16711680
println(hex(Color::BLUE));       // 0xFF
```

---

## Arrays

```raze
var nums = [5, 3, 1, 4, 2];

// Access
nums[0]       // first element
nums[-1]      // last element (negative index)
nums[1..3]    // not built-in; use nums.slice(1,3)

// Methods (return new array unless noted)
nums.len()             // 5
nums.push(6)           // add to end (in-place)
nums.pop()             // remove+return last
nums.shift()           // remove+return first
nums.unshift(0)        // add to front (in-place)
nums.sort()            // sorted copy
nums.sortBy(fn)        // sort by key function
nums.sortInPlace()     // in-place
nums.reverse()         // reversed copy
nums.reverseInPlace()  // in-place
nums.slice(1, 3)       // elements [1..3)
nums.take(3)           // first N
nums.drop(2)           // skip first N
nums.takeWhile(fn)     // take while predicate true
nums.dropWhile(fn)     // drop while predicate true
nums.filter(fn)        // elements where fn returns true
nums.map(fn)           // transform each element
nums.reduce(fn, init)  // fold to single value
nums.forEach(fn)       // iterate (no return)
nums.find(fn)          // first match or null
nums.findIndex(fn)     // index of first match or -1
nums.any(fn)           // true if any matches
nums.all(fn)           // true if all match
nums.none(fn)          // true if none match
nums.count(fn)         // count matching
nums.sum()             // sum of all
nums.min()             // minimum
nums.max()             // maximum
nums.unique()          // deduplicated
nums.flat()            // flatten one level
nums.flatMap(fn)       // map then flatten
nums.zip(other)        // zip with another array
nums.chunk(n)          // split into chunks of size n
nums.groupBy(fn)       // group by key function -> map
nums.toMap(keyFn)      // convert to map by key
nums.contains(v)       // membership check
nums.indexOf(v)        // first index of value
nums.insert(i, v)      // insert at index (in-place)
nums.remove(i)         // remove at index (in-place)
nums.fill(v)           // fill all with value (in-place)
nums.extend(other)     // append another array (in-place)
nums.concat(other)     // concatenation (new array)
nums.copy()            // shallow copy
nums.join(sep)         // join to string
nums.isEmpty()         // true if empty
nums.first()           // first element
nums.last()            // last element
nums.clear()           // empty the array (in-place)

// Array + spread
var a = [1, 2, 3];
var b = [0, ...a, 4];  // [0, 1, 2, 3, 4]
```

---

## Maps

```raze
var m = {"key": "value", "count": 42};

// Access
m["key"]             // "value"
m.key                // same (dot notation)
m["missing"]         // null
m.get("k", "dflt")  // with default

// Methods
m.len()             // number of entries
m.keys()            // array of keys
m.values()          // array of values
m.entries()         // array of [key, value] pairs
m.has("key")        // membership
m.set("k", v)       // set key
m.remove("key")     // delete key
m.clear()           // empty the map
m.merge(other)      // merge in another map (in-place)
m.copy()            // shallow copy
m.invert()          // swap keys and values
m.map(fn)           // transform values
m.filter(fn)        // filter entries
m.forEach(fn)       // iterate (key, value)
m.isEmpty()         // true if empty
```

---

## Strings

```raze
var s = "Hello, World!";

// Indexing
s[0]           // "H"  (1-char string)
s[-1]          // "!"  (negative index)

// Methods
s.len()                  // length
s.upper()                // UPPERCASE
s.lower()                // lowercase
s.trim()                 // strip whitespace both ends
s.ltrim()                // strip left
s.rtrim()                // strip right
s.split(",")             // array of parts
s.replace("a","b")       // replace all
s.replaceFirst("a","b")  // replace first
s.startsWith("He")       // bool
s.endsWith("!")          // bool
s.contains("World")      // bool
s.indexOf("o")           // first index or -1
s.lastIndexOf("o")       // last index or -1
s.count("l")             // count occurrences
s.substr(7, 5)           // substring
s.slice(7, 5)            // same as substr
s.repeat(3)              // "Hello...Hello...Hello..."
s.reverse()              // reversed string
s.padLeft(10, "0")       // pad to width
s.padRight(10, ".")      // pad to width
s.title()                // Title Case
s.center(20, "-")        // centered with fill char
s.isAlpha()              // all alphabetic?
s.isDigit()              // all digits?
s.isAlnum()              // all alphanumeric?
s.isEmpty()              // empty string?
s.chars()                // array of chars
s.bytes()                // array of byte values
s.format(a, b, c)        // replace {} placeholders
s.toInt()                // convert to int
s.toFloat()              // convert to float

// String interpolation (inside double-quoted strings)
"Hello ${name}!"
"${2 + 2} = 4"
"Arr: ${myArr.join(", ")}"
```

---

## Exception Handling

```raze
try {
    if (x == 0) { throw "division by zero"; }
    auto result = riskyOperation();
} catch (err) {
    println("Error: " + str(err));
}

// Throw any value
throw "message";
throw 404;
throw {"code": 500, "msg": "Server Error"};

// assert — throws on false
assert(x > 0, "x must be positive");

// panic / error — always throws
if (badState) { error("Unreachable state reached"); }
```

---

## Native Functions (Raw Address Calls)

```raze
// Declare a native function at a specific memory address
native explode        = (float, float, float, float) -> void @ 0x7F001234;
native getEntityHP    = (int) -> int                 @ 0x7F005678;
native setPosition    = (int, float, float, float) -> void @ 0x7F009ABC;

// Call like any function
explode(128.0, 64.0, 256.0, 15.0);
auto hp = getEntityHP(entityId);
setPosition(playerId, x, y, z);

// Alternately, register from C++ host with full float support:
// interp.registerNative("explode", [](vector<ValPtr> args) {
//     float x = args[0]->toFloat(); ...
// });
//
// Or register raw address (integer/pointer args):
// interp.registerAddr("getHP",
//     reinterpret_cast<uintptr_t>(game_getHP),
//     "int", {"int"});
```

**ABI Notes:**
- Integer/pointer args: direct raw call, zero overhead, all platforms
- Float args: use `registerNative()` C++ callback for correct XMM register placement
- Mixed int+float: link with `-lffi` for full arbitrary signature support

---

## Module System (import)

```raze
import "stdlib/math.rz";
import "stdlib/collections.rz";
import "mylib/utils.rz";
import "../shared/config.rz";

// Modules are cached — importing the same file twice is a no-op
// Paths are resolved relative to the importing file's directory
```

---

## Built-in Functions

### Output
```raze
print(v)          // print without newline
println(v)        // print with newline
eprint(v)         // stderr without newline
eprintln(v)       // stderr with newline
input("prompt")   // read line from stdin
```

### Type System
```raze
typeof(v)         // "int", "float", "bool", "string", "array", "map", "func", "null", ClassName
isNull(v)         // bool
isInt(v)          // bool
isFloat(v)        // bool
isBool(v)         // bool
isString(v)       // bool
isArray(v)        // bool
isMap(v)          // bool
isFunc(v)         // bool
v is TypeName     // bool — type check operator
```

### Conversion
```raze
int(v)            // to int
float(v)          // to float
str(v)            // to string
bool(v)           // to bool
chr(65)           // int to char string: "A"
ord("A")          // char to int: 65
hex(255)          // "0xFF"
bin(42)           // "0b101010"
oct(255)          // "0o377"
parseHex("0xFF")  // 255
parseBin("0b101") // 5
sprintf(fmt, ...) // printf-style formatting: %d %f %.2f %x %s %b
format(tmpl, ...) // replace {} placeholders
```

### Math
```raze
sqrt(x)  abs(x)  pow(a,b)  exp(x)
log(x)   log2(x) log10(x)
sin(x)   cos(x)  tan(x)
asin(x)  acos(x) atan(x)  atan2(y,x)
floor(x) ceil(x) round(x) trunc(x)
hypot(a,b)
min(a,b) max(a,b) clamp(v,lo,hi) lerp(a,b,t)
isNaN(x) isInf(x)

// Constants
PI  TAU  E  INF  NAN_VAL  INT_MAX  INT_MIN
```

### Collections
```raze
array()           // empty array
map()             // empty map
range(end)        // [0..end)
range(start, end) // [start..end)
range(s, e, step) // with step
push(arr, v)      // append (same as arr.push(v))
pop(arr)          // remove last (same as arr.pop())
len(v)            // length of string/array/map
join(arr, sep)    // join array to string
```

### Random
```raze
rand()            // float in [0,1)
rand(n)           // int in [0,n)
rand(lo, hi)      // int in [lo,hi]
randFloat()       // float in [0,1)
randFloat(hi)     // float in [0,hi)
randFloat(lo,hi)  // float in [lo,hi)
shuffle(arr)      // shuffled copy
randomChoice(arr) // random element
```

### Time
```raze
time()            // Unix timestamp (seconds)
clock()           // monotonic milliseconds
clockNs()         // monotonic nanoseconds
sleep(ms)         // sleep milliseconds
```

### File I/O
```raze
readFile(path)         // entire file as string
writeFile(path, str)   // write string to file
appendFile(path, str)  // append string to file
readLines(path)        // array of lines
fileExists(path)       // bool
deleteFile(path)       // bool
listDir(path)          // array of paths
mkDir(path)            // create directory
fileSize(path)         // bytes
pathJoin(a, b, ...)    // cross-platform path join
pathBasename(path)     // filename part
pathDirname(path)      // directory part
pathExtension(path)    // extension (e.g. ".rz")
```

### System
```raze
getenv("VAR")          // environment variable or null
setenv("VAR", "val")   // set environment variable
system("cmd")          // run shell command, return exit code
exec("cmd")            // run command, return stdout string
getCwd()               // current working directory
exit(code)             // exit process
ARGS                   // array of command-line arguments
RAZE_VERSION           // "3.0.0"
RAZE_PLATFORM          // "linux" | "macos" | "windows"
```

### JSON
```raze
toJson(value)          // serialize to JSON string (pretty-printed)
parseJson(str)         // parse JSON string to Raze value
```

### Regex
```raze
regexMatch(str, pattern)            // bool — does pattern match?
regexFind(str, pattern)             // array of all matches
regexReplace(str, pattern, repl)    // replace all matches
```

### Debug / Control
```raze
assert(cond, msg)   // throw if cond is false
error(msg)          // always throw
panic(msg)          // always throw
```

---

## Standard Library (import "stdlib/X.rz")

### math.rz
`sign`, `fmod`, `map_range`, `smoothstep`, `deg2rad`, `rad2deg`, `gcd`, `lcm`,
`isPrime`, `primes`, `factorial`, `fibonacci`, `distance2D`, `distance3D`

### string.rz
`strIsAlpha`, `strIsDigit`, `strIsAlnum`, `strCenter`, `strRepeat`,
`strLines`, `strWords`, `strZfill`, `parseIntSafe`, `parseFloatSafe`

### collections.rz
- `Stack` — push, pop, peek, isEmpty, size, clear, describe
- `Queue` — enqueue, dequeue, front, isEmpty, size, clear, describe
- `Set`   — add, has, remove, values, union, intersect, difference, isSubset, size
- `Deque` — pushFront, pushBack, popFront, popBack, front, back

### algo.rz
`binarySearch`, `quicksort`, `mergesort`, `count`, `deepFlat`,
`zipN`, `windows`, `unique`, `partition`

### vec.rz
- `Vec2` — add, sub, scale, dot, length, normalize, distanceTo, lerp, negate, angle
  - Static: `Vec2::zero()`, `Vec2::one()`, `Vec2::up()`, `Vec2::right()`
- `Vec3` — add, sub, scale, dot, cross, length, normalize, distanceTo, lerp, negate
  - Static: `Vec3::zero()`, `Vec3::one()`, `Vec3::up()`, `Vec3::forward()`, `Vec3::right()`

### event.rz
- `EventEmitter` — on, off, emit, once, listenerCount

---

## Complete Example: Game Entity System

```raze
import "stdlib/vec.rz";

enum EntityType { PLAYER, ENEMY, NPC, PROJECTILE }

class Entity {
    string name;
    float  hp;
    float  maxHp;
    Vec3   position;
    int    entityType;

    func init(string n, float hp, int etype) {
        this.name       = n;
        this.hp         = hp;
        this.maxHp      = hp;
        this.position   = Vec3::zero();
        this.entityType = etype;
    }

    func moveTo(float x, float y, float z) {
        this.position = new Vec3(x, y, z);
    }

    func takeDamage(float dmg) -> float {
        auto actual = clamp(dmg, 0.0, this.hp);
        this.hp -= actual;
        return actual;
    }

    func heal(float amt) {
        this.hp = min(this.hp + amt, this.maxHp);
    }

    func isAlive() -> bool { return this.hp > 0.0; }
    func hpPercent() -> float { return this.hp / this.maxHp * 100.0; }

    func describe() -> string {
        return "[${this.name} hp=${this.hp}/${this.maxHp} pos=${this.position.toString()}]";
    }
}

class Player extends Entity {
    int   score;
    int   kills;
    var   inventory;

    func init(string n) {
        super.init(n, 100.0, EntityType::PLAYER);
        this.score     = 0;
        this.kills     = 0;
        this.inventory = [];
    }

    func attack(Entity target, float power) -> float {
        auto dmg = power * 10.0;
        auto actual = target.takeDamage(dmg);
        this.score += (int)actual;
        if (!target.isAlive()) { this.kills++; }
        return actual;
    }

    func pickUp(var item) { this.inventory.push(item); }
}

func main() {
    auto player = new Player("HeroX");
    player.moveTo(10.0, 0.0, 5.0);

    auto enemies = [
        new Entity("Goblin",  30.0, EntityType::ENEMY),
        new Entity("Dragon", 500.0, EntityType::ENEMY),
        new Entity("Slime",   15.0, EntityType::ENEMY),
    ];

    println("=== Battle Start ===");
    println("Player: " + player.describe());
    println("");

    for (auto enemy in enemies) {
        println("Fighting: " + enemy.describe());
        auto rounds = 0;
        while (enemy.isAlive() && player.isAlive()) {
            auto dmg = player.attack(enemy, 1.5);
            println("  Hit for ${dmg} | Enemy HP: ${enemy.hp}");
            if (enemy.isAlive()) { player.takeDamage(5.0); }
            rounds++;
            if (rounds > 20) { break; }
        }
        if (!enemy.isAlive()) { println("  -> Defeated!"); }
        println("");
    }

    println("Final score: " + str(player.score));
    println("Kills: " + str(player.kills));
    println("HP remaining: " + str(player.hp));
}
```

---

## C++ Host API

```cpp
#include "include/interp.hpp"

Interpreter interp;

// Register a C++ function accessible from Raze
interp.registerNative("myFunc", [](std::vector<ValPtr> args) -> ValPtr {
    float x = args[0]->toFloat();
    float y = args[1]->toFloat();
    return Value::fromFloat(x + y);
});

// Register a raw function address (integer/pointer args)
interp.registerAddr("gameExplode",
    reinterpret_cast<uintptr_t>(game::explode),
    "void",                    // return type
    {"float","float","float","float"}); // param types

// Define global constants
interp.globals->define("GAME_VERSION", Value::fromStr("1.0.0"), true); // const

// Run Raze code
runSource(interp, readFile("myscript.rz"));
```

---

## Known Limitations & Roadmap

- `${}` in string interpolation cannot contain `"` string literals (use variables instead)
- No `async/await` (single-threaded; use callbacks or event emitter)
- No generics/templates
- `defer` is syntactically supported but not yet RAII-scoped (runs immediately)
- Mixed float/int raw native calls need libffi for full support
- No module namespacing (all imports go into global scope)

**Planned for v4:**
- True `defer` with scope cleanup
- Module namespaces: `import "math.rz" as math; math::sin(x);`
- Generics: `func<T> id(T x) -> T { return x; }`
- Pattern matching on types and struct fields
- Optional types: `string?`
- Async/generator support
