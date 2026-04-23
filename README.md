# ⚡ Raze Language (v2.0)

Raze is a fast, C/Java-style scripting language specifically engineered for **game modding**, engine scripting, and performance-critical automation. 

The core philosophy of Raze is providing **zero-overhead interoperability**. Unlike traditional scripting languages that require complex bindings or wrappers, Raze allows you to call native functions at **raw runtime memory addresses** directly from your script.

## 🚀 Key Features

* **Native Raw Calls:** Bind and execute functions at specific memory addresses with zero glue code.
* **Modern Syntax:** A blend of C, Java, and Python-like type inference (`auto`, `val`, `let`).
* **Object-Oriented:** Full class system with inheritance, `super` calls, and constructors.
* **Functional Power:** First-class functions, lambdas, closures, and high-order array methods (`map`, `filter`, `reduce`).
* **Modder Friendly:** Built-in support for hex literals, bitwise operations, and memory-address-based execution.
* **Extensive Stdlib:** Comprehensive modules for Math, Collections (Stack/Queue/Set), and String manipulation.

---

## 🛠️ Installation & Build

### Prerequisites
* `g++` with C++17 support
* `make`

### Building from Source
```bash
git clone [https://github.com/ZNFDev/Raze.git](https://github.com/ZNFDev/Raze.git)
cd raze
make          # Compiles the raze binary
make test     # Runs the test suite

```
### Usage
```bash
./raze script.rz              # Execute a script file
./raze -e 'println("Hello");' # Evaluate inline code
./raze                        # Enter the interactive REPL

```
## 💎 The "Magic": Native Address Calls
Raze was built to interact with running game processes. You can bind a script function to a memory address found via reverse engineering (e.g., Cheat Engine or IDA Pro).
```raze
// Bind a native game function at its runtime address
// Syntax: native [name] = (params) -> return_type @ [address];
native spawn_entity = (int, float, float, float) -> void @ 0x7F1234ABCD;
native get_player_hp = (int) -> int @ 0x7F5566AABB;

func main() {
    // Call the game's internal code directly
    spawn_entity(101, 128.0, 64.0, 256.0);
    
    auto hp = get_player_hp(0);
    println("Player 0 HP: " + str(hp));
}

```
## 📝 Syntax at a Glance
### Classes & OOP
```raze
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

auto p = new Player("Hero", 100.0, "Excalibur");

```
### Functional Arrays
```raze
var nums = [1, 2, 3, 4, 5];

auto evens = nums.filter(func(n, i) => n % 2 == 0);
auto sum   = nums.reduce(func(acc, n, i) => acc + n, 0);

println(nums.sort().join(" | ")); // 1 | 2 | 3 | 4 | 5

```
### Error Handling
```raze
try {
    var data = readFile("config.json");
} catch (err) {
    println("Failed to load: " + err);
}

```
## 📚 Standard Library Reference
| Module | Features |
|---|---|
| **Core** | print, typeof, len, range, sprintf, type casting |
| **Math** | sin, cos, sqrt, clamp, lerp, PI, INF, random |
| **I/O** | readFile, writeFile, readLines, fileExists, listDir |
| **String** | split, trim, replace, format, upper/lower, regex |
| **System** | time, clock, sleep, getenv, system, exit |
## ⚖️ ABI & Native Notes
 * **Integer/Pointer Args:** Direct raw address calls work natively on 64-bit platforms.
 * **Floating Point:** For functions requiring XMM register placement, use the C++ registerNative() API.
 * **Advanced Interop:** For complex mixed-type ABIs, Raze supports linking with libffi.
## 📜 License
This project is licensed under the MIT License - see the LICENSE file for details.
```
