# Объекты: `mold`, наследование, `facet`

## `mold`: поля, конструктор, методы

`mold` описывает тип объектов. Поля объявляются через `let`, конструктор называется так же, как `mold`. Объект создаётся вызовом `Имя(...)` — слова `new` в Glass нет.

```glass run
mold Counter {
    let count: int

    Counter(start: int) {
        count = start
    }

    fn increment() {
        this.count += 1
    }

    fn describe() -> string {
        return "счётчик = " + this.count
    }
}

fn main() {
    let c = Counter(10)
    c.increment()
    c.increment()
    print(c.describe())
    print(c.count)
}
```

```output
счётчик = 12
12
```

Внутри метода поля можно писать просто по имени (`count`), а `this` — ссылка на текущий объект. У `mold` без конструктора объект создаётся вызовом без аргументов:

```glass run
mold Marker {
    fn hello() -> string {
        return "привет"
    }
}

fn main() {
    let m = Marker()
    print(m.hello())
}
```

```output
привет
```

### Методы своего объекта — без `this.`

Метод того же объекта (или его предка) можно вызывать просто по имени: `hello()` означает `this.hello()`, в том числе для виртуальных методов — выбирается реализация реального класса объекта.

```glass run
mold Greeter {
    let name: string

    Greeter(n: string) {
        name = n
    }

    fn hello() -> string {
        return "Привет, " + name
    }

    fn twice() {
        print(hello())
        print(this.hello())
    }
}

mold Stack {
    let items: [int]

    Stack() {
        items = [3, 1, 2]
    }

    fn len() -> int {
        return len(items)
    }

    fn describe() -> string {
        return "в стеке " + this.len() + " элемента"
    }
}

fn main() {
    Greeter("Ада").twice()
    print(Stack().describe())
}
```

```output
Привет, Ада
Привет, Ада
в стеке 3 элемента
```

Метод **не перекрывает** функции, `mold`'ы и встроенные функции с тем же именем: `len(items)` внутри метода `len()` вызывает встроенную `len`, а не сам метод. Чтобы вызвать именно метод, пишите `this.len()`.

## Наследование и `virtual` / `override`

`mold Dog : Animal` — `Dog` наследует поля и методы `Animal`. Конструктор потомка вызывает конструктор предка через `super(...)`. Метод, который потомки могут переопределить, помечается `virtual`; потомок переопределяет его с `override`. Вызов виртуального метода идёт через vtable: выполняется реализация **реального класса объекта**, а не типа переменной.

```glass run
mold Animal {
    let name: string

    Animal(n: string) {
        name = n
    }

    virtual fn speak() -> string {
        return "..."
    }

    fn describe() -> string {
        return this.name + " говорит " + this.speak()
    }
}

mold Dog : Animal {
    Dog(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Гав"
    }
}

mold Cat : Animal {
    Cat(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Мяу"
    }
}

fn main() {
    let pets: [Animal] = [Dog("Рекс"), Cat("Мурка"), Animal("Нечто")]
    for p in pets {
        print(p.describe())
    }
}
```

```output
Рекс говорит Гав
Мурка говорит Мяу
Нечто говорит ...
```

Реализацию предка можно вызвать через `super.метод()`:

```glass run
mold Animal {
    virtual fn describe() -> string {
        return "животное"
    }
}

mold Dog : Animal {
    override fn describe() -> string {
        return "собака, то есть " + super.describe()
    }
}

fn main() {
    let a: Animal = Dog()
    print(a.describe())
}
```

```output
собака, то есть животное
```

### Что виртуально, а что нет

Виртуальны методы с `virtual` или `override`, методы, реализующие контракт `facet` (см. ниже), и `toString()` без параметров. Обычный метод вызывается **по статическому типу переменной** — потомок его «скрывает», а не переопределяет:

```glass run
mold Base {
    fn who() -> string {
        return "base"
    }
}

mold Leaf : Base {
    fn who() -> string {
        return "leaf"
    }
}

fn main() {
    let b: Base = Leaf()
    let l = Leaf()
    print(b.who(), l.who())
}
```

```output
base leaf
```

Чтобы такая тихая путаница не прошла, компилятор строг в двух случаях. `override` допустим только над виртуальным методом:

```glass error
mold Base {
    fn who() -> string {
        return "base"
    }
}

mold Leaf : Base {
    override fn who() -> string {
        return "leaf"
    }
}

fn main() {
}
```

```output
1 семантическая ошибка (декларации):
  8:14: метод 'who' помечен override, но в mold'е 'Base' одноимённый метод не virtual — добавьте virtual (или override) в 'Base', иначе вызов через тип 'Base' не дойдёт до этого метода
```

А метод потомка с той же сигнатурой, что у виртуального метода предка, **обязан** быть помечен `override` — иначе он остался бы мёртвым кодом вне vtable:

```glass error
mold Base {
    virtual fn who() -> string {
        return "base"
    }
}

mold Leaf : Base {
    fn who() -> string {
        return "leaf"
    }
}

fn main() {
}
```

```output
1 семантическая ошибка (декларации):
  8:5: метод 'who' совпадает по сигнатуре с virtual-методом mold'а 'Base' — добавьте override, иначе он не попадёт в vtable и не будет вызываться
```

## Доступ к членам: `public`, `private`, `protected`

Модификатор доступа пишется перед полем, методом или конструктором и решает, откуда к нему можно обратиться. Правила — как в C#:

- `public` и **отсутствие** модификатора — отовсюду;
- `private` — только из кода самого `mold`, в котором член объявлен. Другой объект того же `mold` тоже «свой»: его `private`-поля можно читать и менять;
- `protected` — из самого `mold` и из его потомков.

Свободной функции (например, `main`) доступно только открытое. Обычно данные прячут за `private`, а менять их разрешают методам, которые следят за правилами:

```glass run
mold Account {
    private let balance: int

    Account(initial: int) {
        balance = initial
    }

    private fn isValid(amount: int) -> bool {
        return amount > 0
    }

    fn deposit(amount: int) {
        if isValid(amount) {
            balance += amount
        }
    }

    fn current() -> int {
        return balance
    }

    fn hasMoreThan(other: Account) -> bool {
        return balance > other.balance
    }
}

fn main() {
    let a = Account(100)
    let b = Account(30)
    a.deposit(50)
    a.deposit(-1000)
    print(a.current())
    print(a.hasMoreThan(b))
}
```

```output
150
true
```

Обращение к `private`-члену снаружи — ошибка компиляции, программа не запустится:

```glass error
mold Account {
    private let balance: int

    Account(initial: int) {
        balance = initial
    }
}

fn main() {
    let a = Account(100)
    a.balance = 1000000
}
```

```output
1 ошибка типизации (тела функций):
  11:5: поле 'balance' объявлено private в mold'е 'Account': обращаться к нему можно только из самого 'Account'
```

`protected` пускает потомков (по имени, через `this` и `super`), но не остальной код:

```glass error
mold Animal {
    protected let name: string

    Animal(n: string) {
        name = n
    }
}

mold Dog : Animal {
    Dog(n: string) {
        super(n)
    }

    fn bark() -> string {
        return name + " лает"
    }
}

fn main() {
    let d = Dog("Рекс")
    print(d.bark())
    print(d.name)
}
```

```output
1 ошибка типизации (тела функций):
  22:11: поле 'name' объявлено protected в mold'е 'Animal': доступ только из 'Animal' и его потомков
```

Что ещё важно знать:

- потомок **не видит** `private`-члены предка: обращение к ним — такая же ошибка, а одноимённая функция ими не закрывается;
- `private`-конструктор разрешает создавать объекты только внутри самого `mold`, `protected` — ещё и потомкам (через `super(...)`);
- проверяется место обращения и **статический** тип выражения. В отличие от C#, `protected`-член предка доступен потомку и через ссылку на базовый тип;
- интроспекция показывает все поля независимо от доступа: `~layout` — инструмент для изучения памяти, а не способ обойти инкапсуляцию.

## `static`: общее для всего `mold`

Обычное поле у каждого объекта своё. Если нужна одна переменная на весь `mold` — например, счётчик созданных объектов, — поле объявляют `static`. Оно живёт в отдельной «статической области» с начала программы, поэтому в раскладке объекта (`~layout`) его нет. К нему обращаются по имени `mold`: `Counter.created`; внутри самого `mold` и его потомков — просто по имени.

`static fn` — метод без объекта: у него нет `this`, поэтому он видит только `static`-члены. Вызывается так же: `Counter.total()`, а внутри `mold` — по простому имени.

```glass run
mold Counter {
    static let created: int = 0
    let id: int

    Counter() {
        created += 1
        id = created
    }

    fn myId() -> int {
        return id
    }

    static fn total() -> int {
        return created
    }
}

fn main() {
    print(Counter.total())
    let a = Counter()
    let b = Counter()
    print(a.myId(), b.myId())
    print(Counter.total(), Counter.created)
    print(a~layout)
}
```

```output
0
1 2
2 2
layout(Counter): id@0 (8 байт)
```

Одна переменная на весь `mold` удобна и для «одиночки»: инициализатор `static`-поля — код самого `mold`, поэтому ему доступен даже `private`-конструктор.

```glass run
mold Config {
    static let instance: Config = Config()
    let name: string

    private Config() {
        name = "по умолчанию"
    }

    static fn get() -> Config {
        return instance
    }
}

fn main() {
    print(Config.get().name)
    print(Config.get() == Config.get())
}
```

```output
по умолчанию
true
```

Через объект к `static`-членам обращаться нельзя — только через имя `mold`:

```glass error
mold Counter {
    static let created: int = 0
}

fn main() {
    let c = Counter()
    print(c.created)
}
```

```output
1 ошибка типизации (тела функций):
  7:11: поле 'created' объявлено static: обращаться к нему нужно через имя mold'а — Counter.created
```

Остальные правила:

- Инициализаторы `static`-полей выполняются один раз, **до** `main`: сначала `mold`-предок, потом потомок, внутри `mold` — в порядке объявления. Инициализатор может опираться на уже инициализированные поля; поле без инициализатора получает `0`, `0.0`, `false` или `null`.
- Потомок наследует `static`-члены: `Special.created` — та же переменная, что `Counter.created`. Если потомок объявит своё `static`-поле с тем же именем, это будет отдельная переменная.
- В `static`-коде нет `this` и `super`, а поля и методы объекта по имени не видны — только через переменную с объектом.
- Модификаторы доступа действуют и здесь: `private static` виден только своему `mold`, `protected static` — ещё и потомкам.
- Объект, на который ссылается только `static`-поле, сборщик мусора не тронет: `static`-поля — такие же корни, как переменные в стеке.

## `facet`: контракт без реализации

`facet` — набор методов без тела. `mold`, объявивший `facet` (`mold Circle : Shape`), обязан реализовать все его методы; переменная типа `facet` может хранить любой подходящий объект.

```glass run
facet Shape {
    fn area() -> double;
}

mold Circle : Shape {
    let r: double

    Circle(radius: double) {
        r = radius
    }

    fn area() -> double {
        return 3.0 * r * r
    }
}

mold Square : Shape {
    let s: double

    Square(side: double) {
        s = side
    }

    fn area() -> double {
        return s * s
    }
}

fn main() {
    let shapes: [Shape] = [Circle(1.0), Square(2.0)]
    let total = 0.0
    for sh in shapes {
        total += sh.area()
    }
    print(total)
}
```

```output
7.0
```

## `null` и приведение типов

`null` — «нет объекта»; сравнивается через `==`. Приведение `(Dog) animal` проверяется **во время выполнения** по реальному классу объекта: `null` проходит всегда, чужой класс — ошибка. Заведомо невозможное приведение (число в `mold`, два несвязанных `mold`) компилятор отвергает ещё до запуска.

```glass run
mold Animal {
    Animal() {
    }
}

mold Dog : Animal {
    let tricks: int

    Dog() {
        tricks = 3
    }
}

mold Cat : Animal {
    Cat() {
    }
}

fn main() {
    let a: Animal = Dog()
    let d = (Dog) a
    print(d.tricks)
    let n: Animal = null
    print(n == null)
    let c: Animal = Cat()
    let bad = (Dog) c
    print("сюда не дойдём")
}
```

```output
3
true
Ошибка выполнения: main:26: нельзя привести объект класса 'Cat' к 'Dog'
```

## Как объект превращается в текст: `toString`

`print(объект)` без `toString` показывает класс и адрес. Метод `fn toString() -> string` без параметров вызывается автоматически в `print`, в `+` со строкой, в `+=`, в `str(x)` и `(string) x` — по реальному классу объекта. (Элементы массива печатаются без `toString`.)

```glass run
mold Point {
    let x: int
    let y: int

    Point(px: int, py: int) {
        x = px
        y = py
    }

    fn toString() -> string {
        return "(" + this.x + ", " + this.y + ")"
    }
}

mold Plain {
    let v: int

    Plain(a: int) {
        v = a
    }
}

fn main() {
    let p = Point(1, 2)
    print(p)
    print("точка: " + p)
    print(Plain(7))
}
```

```output
(1, 2)
точка: (1, 2)
Plain@<адрес>
```

`toString` с другой сигнатурой (с параметрами или не `string`) — ошибка компиляции.

## Что дальше

[Исключения](exceptions.md) · [Интроспекция](introspection.md) · [Справочник](../reference/language-reference.md)
