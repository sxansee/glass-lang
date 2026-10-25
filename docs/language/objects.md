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

Внутри метода поля можно писать просто по имени (`count`), а `this` — ссылка на текущий объект. Методы того же объекта вызываются **только через `this.`** — см. «Что пока не поддерживается» ниже. У `mold` без конструктора объект создаётся вызовом без аргументов:

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
1 семантических ошибок (декларации):
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
1 семантических ошибок (декларации):
  8:5: метод 'who' совпадает по сигнатуре с virtual-методом mold'а 'Base' — добавьте override, иначе он не попадёт в vtable и не будет вызываться
```

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

## Что пока не поддерживается

Это известные упрощения; все они записаны в [`TODO.md`](../../TODO.md):

- **Модификаторы доступа.** `public`, `private` и `protected` разбираются, но компилятор их не проверяет: к `private`-полю можно обратиться снаружи. Пока считайте их пометкой для читателя кода.
- **`static`.** Объявление `static fn` разбирается, но вызвать статический метод пока нельзя.
- **Вызов метода без `this.`.** Поле внутри метода можно писать без `this`, а метод того же объекта — только как `this.метод()`; без `this.` компилятор ответит «неизвестная функция».

## Что дальше

[Исключения](exceptions.md) · [Интроспекция](introspection.md) · [Справочник](../reference/language-reference.md)
