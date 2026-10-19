// Содержание курса «Glass с нуля»: 6 глав, 24 урока. Каждый урок = теория (с примерами, которые тесты
// ПРОГОНЯЮТ и сверяют с показанным выводом) + задача + заготовка + решение + подсказка + ожидаемый вывод.
// Тесты (CourseTests) гарантируют: решение проходит проверку, заготовка компилируется, но проверку НЕ проходит
// (иначе задача была бы пустой), все ссылки на ключевые слова в подсказках живые.
//
// Разметка текста — см. Markup.cs. Формат ожидаемого вывода — см. Lesson.cs (префикс «re:» — регулярное выражение).

using System;
using System.Collections.Generic;
using System.Linq;

namespace Glass.Studio.Course
{
    public static class CourseCatalog
    {
        public static IReadOnlyList<Chapter> Chapters { get; } = Build();

        public static IReadOnlyList<Lesson> AllLessons { get; } = Chapters.SelectMany(c => c.Lessons).ToList();

        public static Lesson? Find(string id) => AllLessons.FirstOrDefault(l => l.Id == id);

        private static Lesson L(string id, int chapter, int number, string title, string summary, string theory, string task,
                                string starter, string solution, string hint, string[] expected, string[]? fragments = null) =>
            new Lesson(id, chapter, number, title, summary, theory.Trim(), task.Trim(), starter.Trim() + "\n", solution.Trim() + "\n",
                       hint.Trim(), expected, fragments ?? Array.Empty<string>());

        private static IReadOnlyList<Chapter> Build() => new[]
        {
            new Chapter(1, "Первые шаги", "Вывод, переменные, типы данных", new[]
            {
                L("hello", 1, 1, "Привет, Glass!", "Первая программа: функция main и print.",
"""
Программа на Glass — это набор функций. Выполнение начинается с функции `main`.

```glass
fn main() {
    print("Привет, мир!")
}
```

```output
Привет, мир!
```

- `fn main() { ... }` — точка входа: отсюда всё начинается;
- `print(...)` печатает значение и переходит на новую строку;
- текст (строка) пишется в двойных кавычках.

> Точку с запятой в конце строки ставить не нужно — Glass сам понимает, где кончается оператор. Комментарий начинается с `//` и идёт до конца строки.
""",
"""
Выведите на экран строку **Я учу Glass**.
""",
"""
fn main() {
    // Напишите здесь print(...)
}
""",
"""
fn main() {
    print("Я учу Glass")
}
""",
"Скопируйте пример из теории и замените текст внутри кавычек.",
new[] { "Я учу Glass" }),

                L("variables", 1, 2, "Переменные и типы", "let, int, double, bool, string.",
"""
Переменная хранит значение. Она создаётся словом `let`.

```glass
fn main() {
    let name = "Ада"
    let age: int = 36
    let height = 1.68
    let student = true
    print(name, age, height, student)
}
```

```output
Ада 36 1.68 true
```

У каждой переменной есть **тип**, он не меняется:
- `int` — целые числа (`36`, `-7`);
- `double` — числа с дробной частью (`1.68`);
- `bool` — логические значения `true` и `false`;
- `string` — строки текста.

Тип можно написать явно (`let age: int = 36`) или доверить Glass — он выведет его из значения. Значение переменной можно менять (`age = 37`), но только на значение **того же типа**.

> `print` умеет печатать сразу несколько значений любых типов — через запятую. В выводе они разделяются пробелом.
""",
"""
Объявите переменную **city** со значением `"Москва"` и переменную **year** со значением `1147`. Выведите их одной командой `print`: сначала город, затем год.
""",
"""
fn main() {
    // let city = ...
    // let year = ...
    // print(city, year)
}
""",
"""
fn main() {
    let city = "Москва"
    let year = 1147
    print(city, year)
}
""",
"Две строки с `let`, потом одна `print(city, year)`.",
new[] { "Москва 1147" }, new[] { "let city", "let year" }),

                L("arithmetic", 1, 3, "Арифметика", "Операции, целочисленное деление, остаток.",
"""
В Glass есть привычные операции: `+`, `-`, `*`, `/` и остаток от деления `%`. Скобки меняют порядок действий, как в математике.

```glass
fn main() {
    print(7 + 3 * 2)
    print((7 + 3) * 2)
    print(7 / 2)
    print(7.0 / 2)
    print(7 % 3)
}
```

```output
13
20
3
3.5
1
```

Обратите внимание: `7 / 2` даёт `3` — **целые числа делятся нацело**. Чтобы получить `3.5`, хотя бы одно из чисел должно быть дробным (`7.0 / 2`).

Сокращённая запись: `x += 5` — то же, что `x = x + 5` (есть также `-=`, `*=`, `/=`).
""",
"""
Прямоугольник со сторонами 12 и 5. Выведите его **площадь**, а затем **периметр** — каждое число в своей строке.
""",
"""
fn main() {
    let a = 12
    let b = 5
    // Выведите площадь, затем периметр
}
""",
"""
fn main() {
    let a = 12
    let b = 5
    print(a * b)
    print(2 * (a + b))
}
""",
"Площадь — `a * b`, периметр — `2 * (a + b)`. Два вызова `print`.",
new[] { "60", "34" }),

                L("strings", 1, 4, "Строки и вывод", "Склейка, повтор, длина.",
"""
Строки склеиваются знаком `+`. Если рядом со строкой число (или `true`/`false`), оно **автоматически** превращается в текст.

```glass
fn main() {
    let name = "Ада"
    let age = 36
    print("Меня зовут " + name + ", мне " + age + " лет")
    print("-" * 10)
    print(len(name), "Привет".length)
}
```

```output
Меня зовут Ада, мне 36 лет
----------
3 6
```

- `"-" * 10` — повтор строки;
- `len(s)` или `s.length` — длина строки **в символах** (русские буквы считаются правильно);
- `print(a, b, c)` печатает значения через пробел — часто это удобнее склейки.

> Сложение идёт слева направо: `"a" + 1 + 2` даёт `"a12"`, а `1 + 2 + "a"` — `"3a"`.
""",
"""
Заданы `count = 3` (штук) и `price = 40` (рублей за штуку). Выведите **одну строку** точно в таком виде: `Итого: 3 шт. по 40 руб. = 120 руб.` — сумму посчитайте программой.
""",
"""
fn main() {
    let count = 3
    let price = 40
    // print(...)
}
""",
"""
fn main() {
    let count = 3
    let price = 40
    print("Итого: " + count + " шт. по " + price + " руб. = " + count * price + " руб.")
}
""",
"Либо склейте через `+`, либо передайте части в `print` через запятую. Сумма — `count * price`.",
new[] { "Итого: 3 шт. по 40 руб. = 120 руб." }),

                L("conversions", 1, 5, "Преобразование типов", "int(), double(), str(), bool(), приведение.",
"""
Glass строго следит за типами: строку `"17"` нельзя сложить с числом как число. Для превращений есть функции-преобразователи:

```glass
fn main() {
    let text = "21"
    let n = int(text) * 2
    print(n)
    print(int(3.99))
    print(double(7) / 2)
    print(str(5) + "!")
    print(bool("true"), bool(0))
}
```

```output
42
3
3.5
5!
true false
```

- `int(x)` — в целое: из строки (`"42"`), из дробного (дробная часть **отбрасывается**), из `bool`;
- `double(x)` — в число с дробной частью;
- `str(x)` — в строку (работает с чем угодно);
- `bool(x)` — в логическое: число не равно нулю → `true`; строки `"true"` и `"false"`.

Есть и запись приведения как в других языках: `(int) 3.7`, `(double) n`. Если строку нельзя превратить (`int("abc")`), программа остановится с понятной ошибкой.

> Целое можно сразу положить в `double`-переменную: `let d: double = 5` — Glass сам превратит его в `5.0`.
""",
"""
Строки `"17"` и `"25"` — это числа, записанные текстом. Сложите их **как числа** и выведите сумму.
""",
"""
fn main() {
    let a = "17"
    let b = "25"
    // сложите как числа и выведите сумму
}
""",
"""
fn main() {
    let a = "17"
    let b = "25"
    print(int(a) + int(b))
}
""",
"Если сложить строки `a + b`, получится `\"1725\"`. Сначала превратите каждую в число: `int(a)`.",
new[] { "42" }, new[] { "int(" }),
            }),

            new Chapter(2, "Условия и циклы", "Как заставить программу выбирать и повторяться", new[]
            {
                L("if", 2, 1, "if / else", "Условия, сравнения, && и ||.",
"""
Условие выбирает, какой код выполнить. Условие — всегда значение типа `bool`.

```glass
fn main() {
    let t = 18
    if t < 0 {
        print("мороз")
    } else if t < 25 {
        print("комфортно")
    } else {
        print("жарко")
    }
}
```

```output
комфортно
```

Сравнения: `==`, `!=`, `<`, `>`, `<=`, `>=`. Условия объединяются через `&&` («и»), `||` («или»), а `!` — отрицание.

> `else` пишите на одной строке с закрывающей `}` — как в примере.
""",
"""
Для чисел **a = 3** и **b = 8** выведите слово `нечётное` или `чётное` — по одному слову на число, в порядке a, b. Подсказка: остаток от деления на 2 — это `% 2`.
""",
"""
fn main() {
    let a = 3
    let b = 8
    // для каждого числа: if ... { print(...) } else { print(...) }
}
""",
"""
fn main() {
    let a = 3
    let b = 8
    if a % 2 == 0 {
        print("чётное")
    } else {
        print("нечётное")
    }
    if b % 2 == 0 {
        print("чётное")
    } else {
        print("нечётное")
    }
}
""",
"Чётное число даёт остаток 0 при делении на 2: `a % 2 == 0`.",
new[] { "нечётное", "чётное" }, new[] { "if", "%" }),

                L("while", 2, 2, "Цикл while", "Повторяем, пока условие истинно.",
"""
Цикл `while` повторяет блок, пока условие истинно.

```glass
fn main() {
    let i = 1
    while i <= 3 {
        print("шаг", i)
        i += 1
    }
}
```

```output
шаг 1
шаг 2
шаг 3
```

Внутри цикла обязательно меняйте то, от чего зависит условие (`i += 1`), иначе цикл будет бесконечным. Если программа «зависла» — нажмите **Стоп** (Shift+F5): среда остановит любую программу.
""",
"""
Выведите степени двойки, не превышающие 100: `1 2 4 8 16 32 64` — каждое число в своей строке.
""",
"""
fn main() {
    let p = 1
    // пока p не больше 100: печатаем p и удваиваем его
}
""",
"""
fn main() {
    let p = 1
    while p <= 100 {
        print(p)
        p *= 2
    }
}
""",
"Условие цикла — `p <= 100`, а внутри печатаем `p` и делаем `p *= 2`.",
new[] { "1", "2", "4", "8", "16", "32", "64" }, new[] { "while" }),

                L("for", 2, 3, "for и range", "Перебор чисел, массивов и строк.",
"""
Цикл `for` перебирает значения. `range` создаёт последовательность чисел:

```glass
fn main() {
    for i in range(3) {
        print(i)
    }
    for i in range(2, 5) {
        print(i)
    }
    for i in range(10, 0, -4) {
        print(i)
    }
}
```

```output
0
1
2
2
3
4
10
6
2
```

- `range(n)` — от 0 до n−1;
- `range(a, b)` — от a до b−1;
- `range(a, b, шаг)` — с шагом (он может быть отрицательным).

Перебирать можно и массив, и строку:

```glass
fn main() {
    for x in [10, 20, 30] {
        print(x)
    }
    for c in "Да!" {
        print(c)
    }
}
```

```output
10
20
30
Д
а
!
```

Старая короткая запись тоже работает: `for i in 0..5` — то же, что `range(5)`.
""",
"""
Выведите квадраты чисел от 1 до 5 — каждый в своей строке.
""",
"""
fn main() {
    // for i in range(...) { ... }
}
""",
"""
fn main() {
    for i in range(1, 6) {
        print(i * i)
    }
}
""",
"Числа от 1 до 5 — это `range(1, 6)` (правая граница не входит). В теле цикла печатайте `i * i`.",
new[] { "1", "4", "9", "16", "25" }, new[] { "for" }),

                L("break", 2, 4, "break и continue", "Досрочный выход и пропуск шага.",
"""
`break` немедленно выходит из цикла, `continue` переходит к следующему шагу.

```glass
fn main() {
    for i in range(1, 10) {
        if i % 2 == 0 {
            continue
        }
        if i > 6 {
            break
        }
        print(i)
    }
}
```

```output
1
3
5
```

Здесь чётные числа пропускаются (`continue`), а как только число становится больше 6, цикл заканчивается (`break`).
""",
"""
Переберите числа от 1 до 20. **Пропускайте чётные** (`continue`) и **остановите цикл** (`break`), как только число станет больше 9. Ожидаемый вывод: `1 3 5 7 9` — каждое число в своей строке.
""",
"""
fn main() {
    for i in range(1, 21) {
        // continue — для чётных, break — когда i > 9, иначе print(i)
    }
}
""",
"""
fn main() {
    for i in range(1, 21) {
        if i % 2 == 0 {
            continue
        }
        if i > 9 {
            break
        }
        print(i)
    }
}
""",
"Два `if` в начале тела цикла: первый с `continue`, второй с `break`. Печать — после них.",
new[] { "1", "3", "5", "7", "9" }, new[] { "continue", "break" }),
            }),

            new Chapter(3, "Функции", "Собираем программу из частей", new[]
            {
                L("function", 3, 1, "Своя функция", "fn, вызов, повторное использование.",
"""
Функция — именованный кусок кода, который можно вызвать много раз.

```glass
fn hello() {
    print("Привет!")
}

fn main() {
    hello()
    hello()
}
```

```output
Привет!
Привет!
```

Функция объявляется словом `fn`, вызывается по имени со скобками. Главное преимущество — не копировать один и тот же код: исправили функцию в одном месте, и поправилось везде.
""",
"""
Напишите функцию **line()**, которая печатает строку из пяти дефисов `-----`. В `main` она уже вызывается дважды — вокруг слова Glass.
""",
"""
fn line() {
    // что должна печатать эта функция?
}

fn main() {
    line()
    print("Glass")
    line()
}
""",
"""
fn line() {
    print("-----")
}

fn main() {
    line()
    print("Glass")
    line()
}
""",
"Внутри `line` нужен один вызов `print` со строкой из пяти дефисов.",
new[] { "-----", "Glass", "-----" }),

                L("params", 3, 2, "Параметры и результат", "Типы параметров, ->, return.",
"""
Функция может принимать **параметры** и **возвращать результат**. Типы пишутся явно.

```glass
fn add(a: int, b: int) -> int {
    return a + b
}

fn main() {
    print(add(2, 3))
    print(add(10, add(1, 1)))
}
```

```output
5
12
```

- после имени параметра — двоеточие и тип: `a: int`;
- тип результата — после стрелки `->`;
- `return` возвращает значение и завершает функцию;
- функция без `->` ничего не возвращает.
""",
"""
Допишите две функции: **square(x)** возвращает квадрат числа, **isEven(n)** возвращает `true` для чётного числа. Вывод `main` должен быть: `49`, `true`, `false`.
""",
"""
fn square(x: int) -> int {
    return 0 // замените
}

fn isEven(n: int) -> bool {
    return false // замените
}

fn main() {
    print(square(7))
    print(isEven(10))
    print(isEven(7))
}
""",
"""
fn square(x: int) -> int {
    return x * x
}

fn isEven(n: int) -> bool {
    return n % 2 == 0
}

fn main() {
    print(square(7))
    print(isEven(10))
    print(isEven(7))
}
""",
"`return x * x` и `return n % 2 == 0` — условие само по себе уже значение типа `bool`.",
new[] { "49", "true", "false" }),

                L("recursion", 3, 3, "Рекурсия", "Функция вызывает саму себя.",
"""
Функция может вызывать саму себя — это **рекурсия**. Нужны два ингредиента: условие остановки и шаг к нему.

```glass
fn factorial(n: int) -> int {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

fn main() {
    print(factorial(5))
}
```

```output
120
```

Откройте этот пример в отладчике: поставьте точку останова (F9) на `return 1` и запустите (F5). В панели «Вызовы и переменные» вы увидите **стек вызовов** — пять вложенных `factorial`, у каждого свой `n`.

> Бесконечная рекурсия не вешает среду: после 10 000 вложенных вызовов Glass останавливает программу ошибкой «переполнение стека вызовов».
""",
"""
Допишите **fib(n)** — число Фибоначчи (0, 1, 1, 2, 3, 5, …): `fib(0) = 0`, `fib(1) = 1`, дальше каждое число — сумма двух предыдущих. `main` печатает первые восемь.
""",
"""
fn fib(n: int) -> int {
    return 0 // замените
}

fn main() {
    for i in range(8) {
        print(fib(i))
    }
}
""",
"""
fn fib(n: int) -> int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    for i in range(8) {
        print(fib(i))
    }
}
""",
"Условие остановки: при `n < 2` ответ равен самому `n`. Иначе — `fib(n - 1) + fib(n - 2)`.",
new[] { "0", "1", "1", "2", "3", "5", "8", "13" }),
            }),

            new Chapter(4, "Массивы и строки", "Данные списком: срезы и методы", new[]
            {
                L("arrays", 4, 1, "Массивы", "Литералы, индексы, length, sum/min/max.",
"""
Массив хранит несколько значений **одного типа**. Элементы нумеруются с нуля.

```glass
fn main() {
    let marks = [5, 4, 5, 3]
    print(marks[0], marks[3])
    print(marks.length)
    marks[3] = 4
    for m in marks {
        print(m)
    }
    print(sum(marks), min(marks), max(marks))
}
```

```output
5 3
4
5
4
5
4
18 4 5
```

- `marks[i]` — элемент с номером i; выход за границы — ошибка выполнения;
- `marks.length` (или `len(marks)`) — число элементов;
- `sum`, `min`, `max` работают с массивами чисел;
- пустой массив нужно объявить с типом: `let xs: [int] = []`.
""",
"""
Дан массив оценок `[5, 3, 4, 5, 2]`. Выведите в трёх строках: **наибольшую** оценку, **наименьшую** и **средний балл** (дробное число).
""",
"""
fn main() {
    let marks = [5, 3, 4, 5, 2]
    // max, min и средний балл
}
""",
"""
fn main() {
    let marks = [5, 3, 4, 5, 2]
    print(max(marks))
    print(min(marks))
    print(double(sum(marks)) / len(marks))
}
""",
"Средний балл — `sum(marks) / len(marks)`, но целое делится нацело: сделайте сумму дробной через `double(...)`.",
new[] { "5", "2", "3.8" }),

                L("slices", 4, 2, "Срезы и отрицательные индексы", "a[от:до:шаг], a[-1], разворот.",
"""
Срез берёт часть массива или строки: `a[от:до]` (правая граница не входит). Любую границу можно опустить, а отрицательные числа считаются от конца.

```glass
fn main() {
    let xs = [10, 20, 30, 40, 50]
    print(xs[1:3])
    print(xs[:2])
    print(xs[3:])
    print(xs[-2:])
    print(xs[::2])
    print(xs[::-1])
    print(xs[-1])

    let s = "Привет, мир"
    print(s[:6])
    print(s[::-1])
}
```

```output
[20, 30]
[10, 20]
[40, 50]
[40, 50]
[10, 30, 50]
[50, 40, 30, 20, 10]
50
Привет
рим ,тевирП
```

- `[a:b]` — элементы от a до b−1;
- `[:b]` — с начала, `[a:]` — до конца;
- `[a:b:шаг]` — с шагом; шаг `-1` разворачивает;
- индекс `-1` — последний элемент, `-2` — предпоследний.

Срез — это **новый** массив: изменения среза не затрагивают исходный.
""",
"""
В переменной `word` записано слово «программирование». Выведите в трёх строках: **первые 6 букв**, **последние 5 букв** и слово, **записанное наоборот**.
""",
"""
fn main() {
    let word = "программирование"
    // три среза
}
""",
"""
fn main() {
    let word = "программирование"
    print(word[:6])
    print(word[-5:])
    print(word[::-1])
}
""",
"Первые 6 — `word[:6]`, последние 5 — `word[-5:]`, наоборот — `word[::-1]`.",
new[] { "програ", "вание", "еинавориммаргорп" }),

                L("array-methods", 4, 3, "Методы массивов", "append, pop, insert, remove, sort, join…",
"""
У массивов есть методы — они вызываются через точку:

```glass
fn main() {
    let xs = [5, 3, 8]
    xs.append(1)
    xs.sort()
    print(xs)
    print(xs.pop(), xs)
    xs.insert(0, 99)
    xs.remove(5)
    print(xs, xs.contains(3), xs.indexOf(8))
    xs.reverse()
    print(xs.join(" < "))
}
```

```output
[1, 3, 5, 8]
8 [1, 3, 5]
[99, 1, 3] true -1
3 < 1 < 99
```

- `append(x)` — добавить в конец; `insert(i, x)` — вставить на место i;
- `pop()` / `pop(i)` — убрать и вернуть; `remove(x)` — убрать значение;
- `contains(x)`, `indexOf(x)` — поиск (индекс `-1`, если не нашли);
- `sort()`, `reverse()` — упорядочить / развернуть (меняют сам массив);
- `join(sep)` — склеить элементы в строку; `clear()` — очистить;
- `a + b` склеивает два массива в новый.
""",
"""
Начните с `xs = [5, 3, 8]`. Добавьте в конец число 1, отсортируйте массив и выведите его. Затем выведите его элементы одной строкой через ` < ` (используйте `join`).
""",
"""
fn main() {
    let xs = [5, 3, 8]
    // append, sort, print, join
}
""",
"""
fn main() {
    let xs = [5, 3, 8]
    xs.append(1)
    xs.sort()
    print(xs)
    print(xs.join(" < "))
}
""",
"`xs.append(1)`, затем `xs.sort()`, затем `print(xs)` и `print(xs.join(\" < \"))`.",
new[] { "[1, 3, 5, 8]", "1 < 3 < 5 < 8" }, new[] { "append", "sort", "join" }),

                L("string-methods", 4, 4, "Методы строк", "split, upper, replace, contains…",
"""
Строки тоже умеют многое:

```glass
fn main() {
    let s = "  Привет, Мир  "
    print("[" + s.trim() + "]")
    print(s.trim().upper(), s.trim().lower())
    print("a,b,c".split(","))
    print("мама".replace("м", "р"))
    print("Привет".contains("рив"), "Привет".startsWith("При"), "Привет".indexOf("е"))
    print("ха".repeat(3))
}
```

```output
[Привет, Мир]
ПРИВЕТ, МИР привет, мир
["a", "b", "c"]
рара
true true 4
хахаха
```

- `trim()` — убрать пробелы по краям;
- `upper()`, `lower()` — регистр (в том числе кириллица);
- `split(sep)` — разбить на массив строк; `split("")` — на отдельные буквы;
- `replace(a, b)`, `contains`, `startsWith`, `endsWith`, `indexOf`, `repeat(n)`.

Строка неизменяема: методы возвращают **новую** строку.
""",
"""
Есть строка `text = "to be or not to be"`. Выведите **количество слов** в ней, а затем её же **заглавными буквами**.
""",
"""
fn main() {
    let text = "to be or not to be"
    // split → длина; upper
}
""",
"""
fn main() {
    let text = "to be or not to be"
    print(len(text.split(" ")))
    print(text.upper())
}
""",
"Слова — это `text.split(\" \")`, а их количество — `len(...)` от получившегося массива.",
new[] { "6", "TO BE OR NOT TO BE" }, new[] { "split", "upper" }),
            }),

            new Chapter(5, "Объекты", "mold, наследование и facet", new[]
            {
                L("mold", 5, 1, "mold: поля и конструктор", "Свои типы данных.",
"""
`mold` описывает вид объектов: какие у них **поля** и как их **создавать** (конструктор).

```glass
mold Point {
    let x: int
    let y: int

    Point(px: int, py: int) {
        x = px
        y = py
    }
}

fn main() {
    let p = Point(3, 4)
    print(p.x, p.y)
    p.x = 10
    print(p.x)
}
```

```output
3 4
10
```

- конструктор называется так же, как `mold`; слова `new` в Glass нет — объект создаётся вызовом `Point(3, 4)`;
- поля читаются и меняются через точку;
- объект живёт в **куче**: откройте пример в отладчике — на схеме кучи появится блок `Point` с полями `x` и `y`.
""",
"""
Опишите **mold Book** с полями `title: string` и `pages: int` и конструктором `Book(t, p)`. В `main` создайте книгу `Book("Glass", 120)` и выведите её название и число страниц.
""",
"""
mold Book {
    // поля и конструктор
}

fn main() {
    // let b = Book("Glass", 120)
    // print(b.title, b.pages)
}
""",
"""
mold Book {
    let title: string
    let pages: int

    Book(t: string, p: int) {
        title = t
        pages = p
    }
}

fn main() {
    let b = Book("Glass", 120)
    print(b.title, b.pages)
}
""",
"Два поля `let`, затем конструктор `Book(t: string, p: int) { ... }`, где поля получают значения параметров.",
new[] { "Glass 120" }, new[] { "mold Book" }),

                L("methods", 5, 2, "Методы", "Функции внутри mold.",
"""
Внутри `mold` можно объявлять **методы** — функции, работающие с полями объекта.

```glass
mold Rectangle {
    let w: int
    let h: int

    Rectangle(width: int, height: int) {
        w = width
        h = height
    }

    fn area() -> int {
        return w * h
    }
}

fn main() {
    let r = Rectangle(4, 5)
    print(r.area())
}
```

```output
20
```

Внутри метода поля доступны по имени (`w`, `h`); при желании можно написать `this.w` — «поле этого объекта». Метод вызывается через точку: `r.area()`.
""",
"""
Допишите в **Rectangle** метод `perimeter()` — периметр. `main` выводит площадь и периметр прямоугольника 4 × 5: `20` и `18`.
""",
"""
mold Rectangle {
    let w: int
    let h: int

    Rectangle(width: int, height: int) {
        w = width
        h = height
    }

    fn area() -> int {
        return w * h
    }

    // fn perimeter() -> int { ... }
}

fn main() {
    let r = Rectangle(4, 5)
    print(r.area())
    // print(r.perimeter())
}
""",
"""
mold Rectangle {
    let w: int
    let h: int

    Rectangle(width: int, height: int) {
        w = width
        h = height
    }

    fn area() -> int {
        return w * h
    }

    fn perimeter() -> int {
        return 2 * (w + h)
    }
}

fn main() {
    let r = Rectangle(4, 5)
    print(r.area())
    print(r.perimeter())
}
""",
"Метод пишется так же, как `area`: `fn perimeter() -> int { return 2 * (w + h) }`.",
new[] { "20", "18" }, new[] { "fn perimeter" }),

                L("inheritance", 5, 3, "Наследование и override", "mold B : A, virtual, override, super.",
"""
Один `mold` может **наследовать** другой: `mold Dog : Animal`. Потомок получает поля и методы предка и может **переопределить** метод: в предке метод помечается `virtual`, в потомке — `override`.

```glass
mold Animal {
    let name: string

    Animal(n: string) {
        name = n
    }

    virtual fn speak() -> string {
        return "..."
    }
}

mold Dog : Animal {
    Dog(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Гав!"
    }
}

fn main() {
    let a: Animal = Dog("Рекс")
    print(a.name, a.speak())
}
```

```output
Рекс Гав!
```

- `super(...)` вызывает конструктор предка;
- переменная типа `Animal` может хранить `Dog`, и **какой именно `speak` выполнится, решает реальный класс объекта**. Это и есть виртуальный вызов; в IDE его удобно изучать в «Инспекторе объекта» (таблица vtable).
""",
"""
Допишите **Cat** — наследника Animal, у которого `speak()` возвращает `Мяу!`. `main` перебирает трёх животных и печатает, что говорит каждое. Ожидаемый вывод: `...`, `Гав!`, `Мяу!`.
""",
"""
mold Animal {
    let name: string

    Animal(n: string) {
        name = n
    }

    virtual fn speak() -> string {
        return "..."
    }
}

mold Dog : Animal {
    Dog(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Гав!"
    }
}

mold Cat : Animal {
    Cat(n: string) {
        super(n)
    }
    // переопределите speak()
}

fn main() {
    let zoo = [Animal("Зверь"), Dog("Рекс"), Cat("Мурка")]
    for a in zoo {
        print(a.speak())
    }
}
""",
"""
mold Animal {
    let name: string

    Animal(n: string) {
        name = n
    }

    virtual fn speak() -> string {
        return "..."
    }
}

mold Dog : Animal {
    Dog(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Гав!"
    }
}

mold Cat : Animal {
    Cat(n: string) {
        super(n)
    }

    override fn speak() -> string {
        return "Мяу!"
    }
}

fn main() {
    let zoo = [Animal("Зверь"), Dog("Рекс"), Cat("Мурка")]
    for a in zoo {
        print(a.speak())
    }
}
""",
"Скопируйте метод из `Dog` в `Cat` и поменяйте возвращаемую строку. Слово `override` обязательно.",
new[] { "...", "Гав!", "Мяу!" }, new[] { "override" }),

                L("facet", 5, 4, "facet: контракт", "Интерфейс из методов без реализации.",
"""
`facet` — это **контракт**: набор методов без реализации. Любой `mold`, объявивший этот facet, обязан реализовать все его методы.

```glass
facet Shape {
    fn area() -> double;
}

mold Square : Shape {
    let side: double

    Square(s: double) {
        side = s
    }

    fn area() -> double {
        return side * side
    }
}

mold Rect : Shape {
    let w: double
    let h: double

    Rect(a: double, b: double) {
        w = a
        h = b
    }

    fn area() -> double {
        return w * h
    }
}

fn main() {
    let shapes: [Shape] = [Square(3), Rect(2, 5)]
    for s in shapes {
        print(s.area())
    }
}
```

```output
9.0
10.0
```

Массив `shapes` хранит объекты разных классов под общим типом `Shape`; вызов `s.area()` каждый раз попадает в нужную реализацию.

> Числа `3`, `2`, `5` — целые, но параметры объявлены как `double`: Glass сам превращает их в `3.0`, `2.0`, `5.0`.
""",
"""
Допишите **Triangle** (основание `base`, высота `height`): `area()` — половина произведения. Для `Triangle(6, 3)` площадь `9.0`. `main` выводит площади квадрата 3, прямоугольника 2 × 5 и треугольника: `9.0`, `10.0`, `9.0`.
""",
"""
facet Shape {
    fn area() -> double;
}

mold Square : Shape {
    let side: double

    Square(s: double) {
        side = s
    }

    fn area() -> double {
        return side * side
    }
}

mold Rect : Shape {
    let w: double
    let h: double

    Rect(a: double, b: double) {
        w = a
        h = b
    }

    fn area() -> double {
        return w * h
    }
}

mold Triangle : Shape {
    let b: double
    let h: double

    Triangle(base: double, height: double) {
        b = base
        h = height
    }

    fn area() -> double {
        return 0.0 // замените
    }
}

fn main() {
    let shapes: [Shape] = [Square(3), Rect(2, 5), Triangle(6, 3)]
    for s in shapes {
        print(s.area())
    }
}
""",
"""
facet Shape {
    fn area() -> double;
}

mold Square : Shape {
    let side: double

    Square(s: double) {
        side = s
    }

    fn area() -> double {
        return side * side
    }
}

mold Rect : Shape {
    let w: double
    let h: double

    Rect(a: double, b: double) {
        w = a
        h = b
    }

    fn area() -> double {
        return w * h
    }
}

mold Triangle : Shape {
    let b: double
    let h: double

    Triangle(base: double, height: double) {
        b = base
        h = height
    }

    fn area() -> double {
        return b * h / 2
    }
}

fn main() {
    let shapes: [Shape] = [Square(3), Rect(2, 5), Triangle(6, 3)]
    for s in shapes {
        print(s.area())
    }
}
""",
"Половина произведения основания на высоту: `return b * h / 2` (поля `b` и `h` — `double`, деление выйдет дробным).",
new[] { "9.0", "10.0", "9.0" }),
            }),

            new Chapter(6, "Исключения и устройство памяти", "То, ради чего создан Glass", new[]
            {
                L("exceptions", 6, 1, "try / catch / finally", "Перехват ошибок и свои исключения.",
"""
Ошибки можно **перехватывать**. Код, который может сломаться, помещают в `try`, а обработку — в `catch`.

```glass
fn divide(a: int, b: int) -> int {
    if b == 0 {
        throw Exception("деление на ноль")
    }
    return a / b
}

fn main() {
    try {
        print(divide(10, 2))
        print(divide(1, 0))
        print("сюда не дойдём")
    } catch e: Exception {
        print("ошибка:", e.message)
    } finally {
        print("готово")
    }
}
```

```output
5
ошибка: деление на ноль
готово
```

- `throw Exception("текст")` — выбросить исключение; выполнение прыгает в ближайший подходящий `catch`;
- `catch e: Exception` — перехватить и получить объект `e` (у него есть `message`);
- `finally` выполняется **всегда** — и после ошибки, и после успеха;
- свои исключения — обычный `mold`, наследующий `Exception`: `mold MyError : Exception`.
""",
"""
Допишите функцию **safeDivide(a, b)**: при `b == 0` она бросает исключение с сообщением `деление на ноль`, иначе возвращает частное. `main` уже вызывает её и перехватывает ошибку. Ожидаемый вывод: `5` и `ошибка: деление на ноль`.
""",
"""
fn safeDivide(a: int, b: int) -> int {
    return a / b // добавьте проверку
}

fn main() {
    try {
        print(safeDivide(10, 2))
        print(safeDivide(1, 0))
    } catch e: Exception {
        print("ошибка:", e.message)
    }
}
""",
"""
fn safeDivide(a: int, b: int) -> int {
    if b == 0 {
        throw Exception("деление на ноль")
    }
    return a / b
}

fn main() {
    try {
        print(safeDivide(10, 2))
        print(safeDivide(1, 0))
    } catch e: Exception {
        print("ошибка:", e.message)
    }
}
""",
"В начале функции: `if b == 0 { throw Exception(\"деление на ноль\") }`.",
new[] { "5", "ошибка: деление на ноль" }, new[] { "throw" }),

                L("layout", 6, 2, "Адрес и раскладка: ~layout", "Как объект лежит в памяти.",
"""
Устройство объекта в памяти в Glass можно **увидеть**: для этого есть операторы с тильдой `~`.

```glass
mold Dog {
    let name: string
    let age: int

    Dog(n: string, a: int) {
        name = n
        age = a
    }
}

fn main() {
    let d = Dog("Рекс", 3)
    print(d~layout)
}
```

```output
layout(Dog): name@0 (8 байт), age@1 (8 байт)
```

`d~layout` показывает **раскладку полей**: имя поля, номер слота (`@0`, `@1` — смещение от начала объекта) и размер. У потомка сначала идут унаследованные поля, потом свои.

Ещё операторы:
- `d~address` — адрес объекта в памяти (число, при каждом запуске разное);
- `d~vtable` — таблица виртуальных методов (следующий урок);
- `~heap` и `~gc` — без объекта: про кучу и сборку мусора (через урок).

Откройте пример в отладчике и выберите объект на схеме кучи — **Инспектор объекта** покажет ту же раскладку в виде цветных ячеек.
""",
"""
Создан объект `Point` с полями `x`, `y` (оба `int`). Выведите его раскладку оператором `~layout`.
""",
"""
mold Point {
    let x: int
    let y: int

    Point(px: int, py: int) {
        x = px
        y = py
    }
}

fn main() {
    let p = Point(3, 4)
    // print(p~layout)
}
""",
"""
mold Point {
    let x: int
    let y: int

    Point(px: int, py: int) {
        x = px
        y = py
    }
}

fn main() {
    let p = Point(3, 4)
    print(p~layout)
}
""",
"Оператор пишется сразу после имени объекта, без пробела: `p~layout`.",
new[] { "layout(Point): x@0 (8 байт), y@1 (8 байт)" }, new[] { "~layout" }),

                L("vtable", 6, 3, "Виртуальный вызов: ~vtable", "Что на самом деле делает override.",
"""
Виртуальный вызов — не магия: у каждого класса есть таблица **vtable**, где для каждого виртуального метода записано, чья реализация выполнится. `~vtable` её показывает.

```glass
mold Animal {
    Animal() {
    }

    virtual fn speak() -> string {
        return "..."
    }

    virtual fn eat() -> string {
        return "ням"
    }
}

mold Dog : Animal {
    Dog() {
        super()
    }

    override fn speak() -> string {
        return "Гав!"
    }
}

fn main() {
    let d = Dog()
    print(d~vtable)
}
```

```output
vtable(Dog): eat -> Animal.eat, speak -> Dog.speak
```

Читается так: у `Dog` метод `eat` берётся у `Animal` (унаследован), а `speak` — собственный. Когда вы пишете `a.speak()`, VM смотрит в vtable **реального** класса объекта и вызывает найденную реализацию.
""",
"""
Для заготовки ниже выведите vtable объекта `c` типа `Cat` оператором `~vtable`.
""",
"""
mold Animal {
    Animal() {
    }

    virtual fn speak() -> string {
        return "..."
    }
}

mold Cat : Animal {
    Cat() {
        super()
    }

    override fn speak() -> string {
        return "Мяу!"
    }
}

fn main() {
    let c = Cat()
    // print(c~vtable)
}
""",
"""
mold Animal {
    Animal() {
    }

    virtual fn speak() -> string {
        return "..."
    }
}

mold Cat : Animal {
    Cat() {
        super()
    }

    override fn speak() -> string {
        return "Мяу!"
    }
}

fn main() {
    let c = Cat()
    print(c~vtable)
}
""",
"`print(c~vtable)` — оператор пишется сразу после имени объекта.",
new[] { "vtable(Cat): speak -> Cat.speak" }, new[] { "~vtable" }),

                L("gc", 6, 4, "Куча и сборка мусора", "~heap, ~gc, достижимость.",
"""
Объекты живут в **куче**. Когда на объект не осталось ни одной ссылки, он становится **мусором**, и сборщик (mark-and-sweep) освобождает его память: сначала помечает всё, до чего можно добраться от переменных, потом удаляет непомеченное.

```glass
mold Box {
    let value: int

    Box(v: int) {
        value = v
    }
}

fn main() {
    let a = Box(1)
    let b = Box(2)
    a = b
    ~gc
    print(~heap)
}
```

После `a = b` на `Box(1)` никто не ссылается. `~gc` запускает сборку немедленно, а `~heap` печатает, что осталось в куче: один объект — `heap(1 объект(ов)): Box@…` (адрес при каждом запуске разный).

**Попробуйте в IDE:** поставьте точку останова на `~gc` (F9) и запустите (F5). На схеме кучи объект `Box(1)` будет **красным** — это мусор: он ещё в куче, но уже недостижим. Нажмите «Собрать мусор» — красный узел исчезнет.
""",
"""
Создайте два объекта `Box`, затем «забудьте» первый (`a = b`), вызовите `~gc` и выведите `~heap`. В куче должен остаться один объект.
""",
"""
mold Box {
    let value: int

    Box(v: int) {
        value = v
    }
}

fn main() {
    let a = Box(1)
    let b = Box(2)
    // a = b
    // ~gc
    // print(~heap)
}
""",
"""
mold Box {
    let value: int

    Box(v: int) {
        value = v
    }
}

fn main() {
    let a = Box(1)
    let b = Box(2)
    a = b
    ~gc
    print(~heap)
}
""",
"Три строки: `a = b`, `~gc`, `print(~heap)`.",
new[] { "re:^heap\\(1 объект\\(ов\\)\\): Box@\\d+$" }, new[] { "~gc", "~heap" }),
            }),
        };
    }
}
