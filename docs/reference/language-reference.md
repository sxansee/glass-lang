# Справочник языка Glass

> Файл **генерируется** из `LanguageReference` — тех же записей, по которым IDE строит карточки элементов кода. Не правьте его вручную: измените запись в `dotnet/Glass.Studio.Core/CodeInfo/LanguageReference.cs` и выполните `GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация` (см. [сборку и тесты](../building.md)).

Записей: 76. Подробные объяснения с исполняемыми примерами — на страницах языка из [оглавления](../README.md).

## Содержание

- [Ключевые слова](#ключевые-слова) — `fn`, `let`, `mold`, `facet`, `if`, `else`, `while`, `for`, `in`, `return`, `break`, `continue`, `try`, `catch`, `finally`, `throw`, `this`, `super`
- [Модификаторы](#модификаторы) — `virtual`, `override`, `static`, `public`, `private`, `protected`
- [Типы данных](#типы-данных) — `int`, `double`, `bool`, `string`
- [Значения](#значения) — `true`, `false`, `null`
- [Встроенные константы](#встроенные-константы) — `PI`, `E`
- [Встроенные функции](#встроенные-функции) — `print`, `len`, `str`, `string`, `int`, `double`, `bool`, `range`, `abs`, `min`, `max`, `sum`, `sqrt`, `pow`, `floor`, `ceil`, `round`
- [Методы массивов](#методы-массивов) — `append`, `pop`, `insert`, `remove`, `contains`, `indexOf`, `reverse`, `sort`, `clear`, `join`
- [Методы строк](#методы-строк) — `upper`, `lower`, `trim`, `contains`, `startsWith`, `endsWith`, `indexOf`, `replace`, `split`, `repeat`
- [Свойства](#свойства) — `length`
- [Операторы интроспекции](#операторы-интроспекции) — `~layout`, `~vtable`, `~address`, `~heap`, `~gc`

## Ключевые слова

### `fn`

*ключевое слово*

```
fn имя(параметры) -> тип { ... }
```

Объявляет функцию (или метод внутри mold). Без «-> тип» функция ничего не возвращает. Выполнение программы начинается с fn main().

Пример:

```glass
fn add(a: int, b: int) -> int {
    return a + b
}
```

Подробнее: [language/functions.md](../language/functions.md)

### `let`

*ключевое слово*

```
let имя[: тип] = значение
```

Объявляет переменную (внутри mold — поле). Тип можно не писать: он выводится из значения и дальше не меняется.

Пример:

```glass
let age: int = 36
let name = "Ада"
```

Подробнее: [language/basics.md](../language/basics.md)

### `mold`

*ключевое слово*

```
mold Имя [: Предок] { поля, конструктор, методы }
```

Описывает тип объектов: поля, конструктор и методы. Объект создаётся вызовом Имя(...) — слова new в Glass нет. Наследование: mold Dog : Animal.

Пример:

```glass
mold Point {
    let x: int
    let y: int
    Point(px: int, py: int) {
        x = px
        y = py
    }
}
```

Подробнее: [language/objects.md](../language/objects.md)

### `facet`

*ключевое слово*

```
facet Имя { fn метод() -> тип; }
```

Контракт: набор методов без реализации. mold, объявивший facet (mold Square : Shape), обязан реализовать все его методы. Переменная типа facet хранит любой подходящий объект.

Пример:

```glass
facet Shape {
    fn area() -> double;
}
```

Подробнее: [language/objects.md](../language/objects.md)

### `if`

*ключевое слово*

```
if условие { ... } else if условие { ... } else { ... }
```

Выполняет блок, если условие (значение bool) истинно. else можно писать на одной строке с закрывающей } или на следующей.

Пример:

```glass
if x > 0 {
    print("плюс")
} else {
    print("не плюс")
}
```

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `else`

*ключевое слово*

```
} else { ... }
```

Ветка if, которая выполняется, когда условие ложно. Можно писать на одной строке с закрывающей } предыдущего блока или на следующей.

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `while`

*ключевое слово*

```
while условие { ... }
```

Повторяет блок, пока условие истинно. Не забудьте менять то, от чего оно зависит, иначе цикл бесконечный (остановить программу можно кнопкой «Стоп»).

Пример:

```glass
let i = 0
while i < 3 {
    print(i)
    i += 1
}
```

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `for`

*ключевое слово*

```
for x in коллекция { ... }
```

Перебирает элементы массива, символы строки, числа range(...) или диапазон a..b. Переменная цикла получает новое значение на каждом шаге.

Пример:

```glass
for i in range(3) {
    print(i)
}
```

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `in`

*ключевое слово*

```
for x in коллекция
```

Часть цикла for: что именно перебирать.

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `return`

*ключевое слово*

```
return значение
```

Завершает функцию и возвращает значение. В функции без результата — просто return.

Подробнее: [language/functions.md](../language/functions.md)

### `break`

*ключевое слово*

```
break
```

Немедленно выходит из ближайшего цикла.

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `continue`

*ключевое слово*

```
continue
```

Переходит к следующему шагу ближайшего цикла.

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `try`

*ключевое слово*

```
try { ... } catch e: Тип { ... } finally { ... }
```

Начинает блок, ошибки в котором можно перехватить. Ошибку бросает throw; ловит ближайший подходящий catch.

Пример:

```glass
try {
    throw Exception("беда")
} catch e: Exception {
    print(e.message)
}
```

Подробнее: [language/exceptions.md](../language/exceptions.md)

### `catch`

*ключевое слово*

```
catch e: Тип { ... }
```

Перехватывает исключение указанного типа (и его потомков). e — объект исключения, у него есть поле message.

Подробнее: [language/exceptions.md](../language/exceptions.md)

### `finally`

*ключевое слово*

```
finally { ... }
```

Блок, который выполняется ВСЕГДА — и после успеха, и после ошибки. (return, break и continue внутри try с finally пока не поддерживаются.)

Подробнее: [language/exceptions.md](../language/exceptions.md)

### `throw`

*ключевое слово*

```
throw Exception("текст")
```

Выбрасывает исключение: выполнение прыгает в ближайший подходящий catch. Свои исключения — mold, наследующий Exception.

Подробнее: [language/exceptions.md](../language/exceptions.md)

### `this`

*ключевое слово*

```
this.поле
```

Ссылка на текущий объект внутри метода или конструктора. Поля и методы того же объекта можно писать и без this; он нужен, когда имя закрыто чем-то другим — параметром или функцией с тем же именем (скажем, встроенной len).

Подробнее: [language/objects.md](../language/objects.md)

### `super`

*ключевое слово*

```
super(аргументы)
```

В конструкторе потомка вызывает конструктор предка. Метод предка вызывается как super.метод(...).

Подробнее: [language/objects.md](../language/objects.md)

## Модификаторы

### `virtual`

*модификатор*

```
virtual fn имя() -> тип
```

Помечает метод, который потомки могут переопределить. Вызов идёт через vtable: выполняется реализация РЕАЛЬНОГО класса объекта. Виртуальны сами методы, реализующие facet, и toString() без параметров; обычный метод вызывается по статическому типу переменной.

Пример:

```glass
virtual fn speak() -> string {
    return "..."
}
```

Подробнее: [language/objects.md](../language/objects.md)

### `override`

*модификатор*

```
override fn имя() -> тип
```

Переопределяет виртуальный метод предка (с virtual или override, реализацию facet или toString()). Сигнатура должна совпадать. Над обычным методом override — ошибка; метод потомка с той же сигнатурой, что у виртуального метода предка, без override — тоже ошибка.

Подробнее: [language/objects.md](../language/objects.md)

### `static`

*модификатор*

```
static fn имя()
```

Член принадлежит самому mold, а не объектам. Статический метод не входит в vtable. Пока объявление разбирается, но вызвать статический метод нельзя.

Подробнее: [language/objects.md](../language/objects.md)

### `public`

*модификатор*

```
public let имя: тип
```

Модификатор доступа: член доступен из любого места. Член без модификатора доступа ведёт себя так же.

Подробнее: [language/objects.md](../language/objects.md)

### `private`

*модификатор*

```
private let имя: тип
```

Модификатор доступа: член доступен только коду самого mold, где он объявлен (в том числе другим объектам этого же mold). Обращение снаружи и из потомков — ошибка компиляции. То же для методов и конструкторов.

Пример:

```glass
mold Account {
    private let balance: int

    fn current() -> int {
        return balance
    }
}
```

Подробнее: [language/objects.md](../language/objects.md)

### `protected`

*модификатор*

```
protected let имя: тип
```

Модификатор доступа: член доступен самому mold и его потомкам; из остального кода обращение — ошибка компиляции. То же для методов и конструкторов.

Пример:

```glass
mold Animal {
    protected let name: string
}
```

Подробнее: [language/objects.md](../language/objects.md)

## Типы данных

### `int`

*тип данных*

```
int
```

Целое число (64 бита). Деление целых — нацело: 7 / 2 == 3. Преобразование: int(x) или (int) x.

Пример:

```glass
let n: int = 42
```

Подробнее: [language/basics.md](../language/basics.md)

### `double`

*тип данных*

```
double
```

Число с дробной частью. Печатается всегда с точкой: 3.0, 2.5. Целое можно положить в double-переменную — оно превратится само.

Пример:

```glass
let h: double = 1.68
```

Подробнее: [language/basics.md](../language/basics.md)

### `bool`

*тип данных*

```
bool
```

Логическое значение: true или false. Результат сравнений; нужен для if и while.

Пример:

```glass
let ok: bool = 3 > 2
```

Подробнее: [language/basics.md](../language/basics.md)

### `string`

*тип данных*

```
string
```

Строка текста в кавычках. Индексы, длина и срезы считаются в символах, поэтому русские буквы работают правильно. Склеивается через +, повторяется через *.

Пример:

```glass
let s = "Привет"
```

Подробнее: [language/strings.md](../language/strings.md)

## Значения

### `true`

*значение*

```
true
```

Логическое «истина» (тип bool).

Подробнее: [language/basics.md](../language/basics.md)

### `false`

*значение*

```
false
```

Логическое «ложь» (тип bool).

Подробнее: [language/basics.md](../language/basics.md)

### `null`

*значение*

```
null
```

Пустая ссылка — «объекта нет». Допустима для объектов и массивов; обращение к полю или методу через null — ошибка выполнения.

Пример:

```glass
let p: Point = null
```

Подробнее: [language/objects.md](../language/objects.md)

## Встроенные константы

### `PI`

*встроенная константа*

```
PI
```

Число π ≈ 3.14159265358979 (double).

Пример:

```glass
let area = PI * r * r
```

### `E`

*встроенная константа*

```
E
```

Число e ≈ 2.71828182845905 (double).

## Встроенные функции

### `print`

*встроенная функция*

```
print(значения...)
```

Печатает любое число значений любых типов через пробел и переходит на новую строку. print() без аргументов печатает пустую строку. Объект без toString() печатается как Класс@0x…, с toString() — его результатом.

Пример:

```glass
print("x =", 5, true)  // x = 5 true
```

### `len`

*встроенная функция*

```
len(x) -> int
```

Длина массива (число элементов) или строки (число символов). То же, что x.length.

Пример:

```glass
len([1, 2, 3])  // 3
len("Привет")  // 6
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `str`

*встроенная функция*

```
str(x) -> string
```

Превращает значение любого типа в строку так же, как print его показывает. Синоним: string(x).

Пример:

```glass
str(5) + "!"  // "5!"
```

Подробнее: [language/basics.md](../language/basics.md)

### `string`

*встроенная функция*

```
string(x) -> string
```

То же, что str(x): значение любого типа → строка.

Подробнее: [language/basics.md](../language/basics.md)

### `int`

*встроенная функция*

```
int(x) -> int
```

Преобразует в целое: из строки ("42"), из double (дробная часть отбрасывается к нулю), из bool. Непригодная строка — ошибка выполнения.

Пример:

```glass
int("42")  // 42
int(3.99)  // 3
```

Подробнее: [language/basics.md](../language/basics.md)

### `double`

*встроенная функция*

```
double(x) -> double
```

Преобразует в число с дробной частью: из int, bool, строки ("2.5").

Пример:

```glass
double(7) / 2  // 3.5
```

Подробнее: [language/basics.md](../language/basics.md)

### `bool`

*встроенная функция*

```
bool(x) -> bool
```

Преобразует в bool: число не равно нулю → true; строки "true" и "false" (регистр не важен).

Пример:

```glass
bool(0)  // false
```

Подробнее: [language/basics.md](../language/basics.md)

### `range`

*встроенная функция*

```
range(стоп) | range(старт, стоп) | range(старт, стоп, шаг) -> [int]
```

Последовательность целых чисел (правая граница не входит). Шаг может быть отрицательным, но не нулём. В цикле for массив не создаётся.

Пример:

```glass
for i in range(2, 10, 3) {
    print(i)   // 2 5 8
}
```

Подробнее: [language/control-flow.md](../language/control-flow.md)

### `abs`

*встроенная функция*

```
abs(x)
```

Модуль числа; тип результата — тот же, что у аргумента.

Пример:

```glass
abs(-3)  // 3
```

### `min`

*встроенная функция*

```
min(a, b) | min(массив)
```

Наименьшее из двух чисел или из элементов массива чисел. Для пустого массива — ошибка.

Пример:

```glass
min(3, 5)  // 3
min([4, 2, 9])  // 2
```

### `max`

*встроенная функция*

```
max(a, b) | max(массив)
```

Наибольшее из двух чисел или из элементов массива чисел. Для пустого массива — ошибка.

Пример:

```glass
max(3, 2.5)  // 3.0
```

### `sum`

*встроенная функция*

```
sum(массив чисел)
```

Сумма элементов массива: int для [int], double для [double]. Сумма пустого массива — 0.

Пример:

```glass
sum([1, 2, 3])  // 6
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `sqrt`

*встроенная функция*

```
sqrt(x) -> double
```

Квадратный корень. Для отрицательного числа — ошибка выполнения.

Пример:

```glass
sqrt(16)  // 4.0
```

### `pow`

*встроенная функция*

```
pow(основание, степень) -> double
```

Возведение в степень; результат всегда double.

Пример:

```glass
pow(2, 10)  // 1024.0
```

### `floor`

*встроенная функция*

```
floor(x) -> int
```

Округление вниз до целого.

Пример:

```glass
floor(2.7)  // 2
```

### `ceil`

*встроенная функция*

```
ceil(x) -> int
```

Округление вверх до целого.

Пример:

```glass
ceil(2.1)  // 3
```

### `round`

*встроенная функция*

```
round(x) -> int
```

Округление до ближайшего целого; половина — от нуля: round(2.5) == 3, round(-2.5) == -3.

Пример:

```glass
round(2.5)  // 3
```

## Методы массивов

### `append`

*метод массива*

```
xs.append(x)
```

Добавляет элемент в конец массива. Тип x должен подходить к типу элементов.

Пример:

```glass
xs.append(4)
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `pop`

*метод массива*

```
xs.pop() | xs.pop(i) -> элемент
```

Убирает и возвращает последний элемент (или элемент с индексом i; отрицательный — от конца). Из пустого массива — ошибка.

Пример:

```glass
let last = xs.pop()
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `insert`

*метод массива*

```
xs.insert(i, x)
```

Вставляет x на место i; остальные сдвигаются. Индекс за границами «прижимается» к краю.

Пример:

```glass
xs.insert(0, 99)
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `remove`

*метод массива*

```
xs.remove(x)
```

Убирает первый элемент, равный x. Если такого нет — ошибка выполнения.

Пример:

```glass
xs.remove(5)
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `contains`

*метод массива*

```
xs.contains(x) -> bool
```

Есть ли элемент в массиве.

Пример:

```glass
xs.contains(3)
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `indexOf`

*метод массива*

```
xs.indexOf(x) -> int
```

Индекс первого элемента, равного x, или -1, если такого нет.

Пример:

```glass
xs.indexOf(8)
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `reverse`

*метод массива*

```
xs.reverse()
```

Разворачивает массив на месте. Чтобы получить развёрнутую копию, используйте срез xs[::-1].

Пример:

```glass
xs.reverse()
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `sort`

*метод массива*

```
xs.sort()
```

Сортирует массив на месте по возрастанию (числа или строки). Сортировка устойчивая.

Пример:

```glass
xs.sort()
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `clear`

*метод массива*

```
xs.clear()
```

Удаляет все элементы.

Пример:

```glass
xs.clear()
```

Подробнее: [language/arrays.md](../language/arrays.md)

### `join`

*метод массива*

```
xs.join(разделитель) -> string
```

Склеивает элементы в одну строку через разделитель (числа превращаются в текст).

Пример:

```glass
[1, 2, 3].join("-")  // "1-2-3"
```

Подробнее: [language/arrays.md](../language/arrays.md)

## Методы строк

### `upper`

*метод строки*

```
s.upper() -> string
```

Строка заглавными буквами (латиница, кириллица, греческий).

Пример:

```glass
"привет".upper()  // "ПРИВЕТ"
```

Подробнее: [language/strings.md](../language/strings.md)

### `lower`

*метод строки*

```
s.lower() -> string
```

Строка строчными буквами.

Подробнее: [language/strings.md](../language/strings.md)

### `trim`

*метод строки*

```
s.trim() -> string
```

Убирает пробелы и переводы строк по краям.

Пример:

```glass
"  hi ".trim()  // "hi"
```

Подробнее: [language/strings.md](../language/strings.md)

### `contains`

*метод строки*

```
s.contains(часть) -> bool
```

Встречается ли подстрока в строке.

Пример:

```glass
"hello".contains("ell")  // true
```

Подробнее: [language/strings.md](../language/strings.md)

### `startsWith`

*метод строки*

```
s.startsWith(начало) -> bool
```

Начинается ли строка с указанного текста.

Подробнее: [language/strings.md](../language/strings.md)

### `endsWith`

*метод строки*

```
s.endsWith(конец) -> bool
```

Заканчивается ли строка указанным текстом.

Подробнее: [language/strings.md](../language/strings.md)

### `indexOf`

*метод строки*

```
s.indexOf(часть) -> int
```

Номер символа, с которого начинается первое вхождение подстроки, или -1.

Пример:

```glass
"привет".indexOf("в")  // 3
```

Подробнее: [language/strings.md](../language/strings.md)

### `replace`

*метод строки*

```
s.replace(что, на_что) -> string
```

Заменяет все вхождения подстроки. Строки неизменяемы: метод возвращает новую строку.

Пример:

```glass
"мама".replace("м", "р")  // "рара"
```

Подробнее: [language/strings.md](../language/strings.md)

### `split`

*метод строки*

```
s.split(разделитель) -> [string]
```

Разбивает строку на массив строк. Пустой разделитель — на отдельные символы.

Пример:

```glass
"a,b,c".split(",")  // ["a", "b", "c"]
```

Подробнее: [language/strings.md](../language/strings.md)

### `repeat`

*метод строки*

```
s.repeat(n) -> string
```

Повторяет строку n раз (то же, что s * n).

Пример:

```glass
"ха".repeat(3)  // "хахаха"
```

Подробнее: [language/strings.md](../language/strings.md)

## Свойства

### `length`

*свойство*

```
x.length -> int
```

Длина массива (число элементов) или строки (число символов). Это свойство — пишется без скобок.

Пример:

```glass
[1, 2, 3].length  // 3
```

Подробнее: [language/arrays.md](../language/arrays.md)

## Операторы интроспекции

### `~layout`

*оператор интроспекции*

```
объект~layout
```

Раскладка полей объекта в памяти: имя поля, номер слота (@0, @1 — смещение) и размер. Унаследованные поля идут первыми.

Пример:

```glass
print(d~layout)
// layout(Dog): name@0 (8 байт), age@1 (8 байт)
```

Подробнее: [language/introspection.md](../language/introspection.md)

### `~vtable`

*оператор интроспекции*

```
объект~vtable
```

Таблица виртуальных методов класса объекта: для каждого метода — чья реализация реально выполнится (свой класс или унаследованная от предка).

Пример:

```glass
print(d~vtable)
// vtable(Dog): eat -> Animal.eat, speak -> Dog.speak
```

Подробнее: [language/introspection.md](../language/introspection.md)

### `~address`

*оператор интроспекции*

```
объект~address
```

Адрес объекта в куче (число). При каждом запуске разный; одинаковый адрес — один и тот же объект.

Пример:

```glass
print(d~address)
```

Подробнее: [language/introspection.md](../language/introspection.md)

### `~heap`

*оператор интроспекции*

```
~heap
```

Снимок кучи: сколько объектов живо и какие. Не требует объекта.

Пример:

```glass
print(~heap)
// heap(2 объект(ов)): Box@..., Box@...
```

Подробнее: [language/introspection.md](../language/introspection.md)

### `~gc`

*оператор интроспекции*

```
~gc
```

Запускает сборку мусора немедленно: недостижимые объекты освобождаются. Не требует объекта. В IDE то же делает кнопка «Собрать мусор».

Пример:

```glass
a = b
~gc
```

Подробнее: [language/introspection.md](../language/introspection.md)
