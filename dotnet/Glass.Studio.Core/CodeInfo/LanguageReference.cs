// Справочник языка Glass: всё, что можно «показать по наведению»: ключевые слова, типы, встроенные функции, методы
// массивов и строк, операторы интроспекции, константы. Единственный источник для:
//   • карточки элемента кода по правой кнопке мыши в редакторе (CodeInfoProvider);
//   • справочника docs/reference/language-reference.md — он ГЕНЕРИРУЕТСЯ отсюда (см. тест «документация актуальна»),
//     поэтому документация и подсказки в IDE не могут разойтись.
// Тесты следят, чтобы здесь было ВСЁ, что есть в языке: каждое ключевое слово лексера и каждая встроенная
// функция/метод из bytecode/Builtins.h.

using System;
using System.Collections.Generic;
using System.Linq;

namespace Glass.Studio.CodeInfo
{
    public enum ReferenceKind
    {
        Keyword, Modifier, Type, Literal, Function, ArrayMethod, StringMethod, Property, Introspection, Constant,
    }

    public sealed record ReferenceEntry(string Name, ReferenceKind Kind, string Signature, string Description, string? Example, string DocTopic)
    {
        public string KindText => Kind switch
        {
            ReferenceKind.Keyword => "ключевое слово",
            ReferenceKind.Modifier => "модификатор",
            ReferenceKind.Type => "тип данных",
            ReferenceKind.Literal => "значение",
            ReferenceKind.Function => "встроенная функция",
            ReferenceKind.ArrayMethod => "метод массива",
            ReferenceKind.StringMethod => "метод строки",
            ReferenceKind.Property => "свойство",
            ReferenceKind.Introspection => "оператор интроспекции",
            _ => "встроенная константа",
        };
    }

    public static class LanguageReference
    {
        private static ReferenceEntry E(string name, ReferenceKind kind, string signature, string description, string? example, string topic) =>
            new ReferenceEntry(name, kind, signature, description, example, topic);

        public static IReadOnlyList<ReferenceEntry> All { get; } = new[]
        {
            // ---------------------------------------------------------------- ключевые слова
            E("fn", ReferenceKind.Keyword, "fn имя(параметры) -> тип { ... }", "Объявляет функцию (или метод внутри mold). Без «-> тип» функция ничего не возвращает. Выполнение программы начинается с fn main().", "fn add(a: int, b: int) -> int {\n    return a + b\n}", "language/functions.md"),
            E("let", ReferenceKind.Keyword, "let имя[: тип] = значение", "Объявляет переменную (внутри mold — поле). Тип можно не писать: он выводится из значения и дальше не меняется.", "let age: int = 36\nlet name = \"Ада\"", "language/basics.md"),
            E("mold", ReferenceKind.Keyword, "mold Имя [: Предок] { поля, конструктор, методы }", "Описывает тип объектов: поля, конструктор и методы. Объект создаётся вызовом Имя(...) — слова new в Glass нет. Наследование: mold Dog : Animal.", "mold Point {\n    let x: int\n    let y: int\n    Point(px: int, py: int) {\n        x = px\n        y = py\n    }\n}", "language/objects.md"),
            E("facet", ReferenceKind.Keyword, "facet Имя { fn метод() -> тип; }", "Контракт: набор методов без реализации. mold, объявивший facet (mold Square : Shape), обязан реализовать все его методы. Переменная типа facet хранит любой подходящий объект.", "facet Shape {\n    fn area() -> double;\n}", "language/objects.md"),
            E("if", ReferenceKind.Keyword, "if условие { ... } else if условие { ... } else { ... }", "Выполняет блок, если условие (значение bool) истинно. else можно писать на одной строке с закрывающей } или на следующей.", "if x > 0 {\n    print(\"плюс\")\n} else {\n    print(\"не плюс\")\n}", "language/control-flow.md"),
            E("else", ReferenceKind.Keyword, "} else { ... }", "Ветка if, которая выполняется, когда условие ложно. Можно писать на одной строке с закрывающей } предыдущего блока или на следующей.", null, "language/control-flow.md"),
            E("while", ReferenceKind.Keyword, "while условие { ... }", "Повторяет блок, пока условие истинно. Не забудьте менять то, от чего оно зависит, иначе цикл бесконечный (остановить программу можно кнопкой «Стоп»).", "let i = 0\nwhile i < 3 {\n    print(i)\n    i += 1\n}", "language/control-flow.md"),
            E("for", ReferenceKind.Keyword, "for x in коллекция { ... }", "Перебирает элементы массива, символы строки, числа range(...) или диапазон a..b. Переменная цикла получает новое значение на каждом шаге.", "for i in range(3) {\n    print(i)\n}", "language/control-flow.md"),
            E("in", ReferenceKind.Keyword, "for x in коллекция", "Часть цикла for: что именно перебирать.", null, "language/control-flow.md"),
            E("return", ReferenceKind.Keyword, "return значение", "Завершает функцию и возвращает значение. В функции без результата — просто return.", null, "language/functions.md"),
            E("break", ReferenceKind.Keyword, "break", "Немедленно выходит из ближайшего цикла.", null, "language/control-flow.md"),
            E("continue", ReferenceKind.Keyword, "continue", "Переходит к следующему шагу ближайшего цикла.", null, "language/control-flow.md"),
            E("try", ReferenceKind.Keyword, "try { ... } catch e: Тип { ... } finally { ... }", "Начинает блок, ошибки в котором можно перехватить. Ошибку бросает throw; ловит ближайший подходящий catch.", "try {\n    throw Exception(\"беда\")\n} catch e: Exception {\n    print(e.message)\n}", "language/exceptions.md"),
            E("catch", ReferenceKind.Keyword, "catch e: Тип { ... }", "Перехватывает исключение указанного типа (и его потомков). e — объект исключения, у него есть поле message.", null, "language/exceptions.md"),
            E("finally", ReferenceKind.Keyword, "finally { ... }", "Блок, который выполняется ВСЕГДА — и после успеха, и после ошибки. (return, break и continue внутри try с finally пока не поддерживаются.)", null, "language/exceptions.md"),
            E("throw", ReferenceKind.Keyword, "throw Exception(\"текст\")", "Выбрасывает исключение: выполнение прыгает в ближайший подходящий catch. Свои исключения — mold, наследующий Exception.", null, "language/exceptions.md"),
            E("this", ReferenceKind.Keyword, "this.поле", "Ссылка на текущий объект внутри метода или конструктора. Поле можно писать и без this, а метод того же объекта вызывается только как this.метод().", null, "language/objects.md"),
            E("super", ReferenceKind.Keyword, "super(аргументы)", "В конструкторе потомка вызывает конструктор предка. Метод предка вызывается как super.метод(...).", null, "language/objects.md"),

            // ---------------------------------------------------------------- модификаторы
            E("virtual", ReferenceKind.Modifier, "virtual fn имя() -> тип", "Помечает метод, который потомки могут переопределить. Вызов идёт через vtable: выполняется реализация РЕАЛЬНОГО класса объекта. Виртуальны сами методы, реализующие facet, и toString() без параметров; обычный метод вызывается по статическому типу переменной.", "virtual fn speak() -> string {\n    return \"...\"\n}", "language/objects.md"),
            E("override", ReferenceKind.Modifier, "override fn имя() -> тип", "Переопределяет виртуальный метод предка (с virtual или override, реализацию facet или toString()). Сигнатура должна совпадать. Над обычным методом override — ошибка; метод потомка с той же сигнатурой, что у виртуального метода предка, без override — тоже ошибка.", null, "language/objects.md"),
            E("static", ReferenceKind.Modifier, "static fn имя()", "Член принадлежит самому mold, а не объектам. Статический метод не входит в vtable. Пока объявление разбирается, но вызвать статический метод нельзя.", null, "language/objects.md"),
            E("public", ReferenceKind.Modifier, "public", "Модификатор доступа члена mold. Пока разбирается, но не проверяется: доступ к членам снаружи не запрещён.", null, "language/objects.md"),
            E("private", ReferenceKind.Modifier, "private", "Модификатор доступа члена mold. Пока разбирается, но не проверяется: к private-члену можно обратиться снаружи.", null, "language/objects.md"),
            E("protected", ReferenceKind.Modifier, "protected", "Модификатор доступа члена mold. Пока разбирается, но не проверяется: доступ из других классов не запрещён.", null, "language/objects.md"),

            // ---------------------------------------------------------------- типы
            E("int", ReferenceKind.Type, "int", "Целое число (64 бита). Деление целых — нацело: 7 / 2 == 3. Преобразование: int(x) или (int) x.", "let n: int = 42", "language/basics.md"),
            E("double", ReferenceKind.Type, "double", "Число с дробной частью. Печатается всегда с точкой: 3.0, 2.5. Целое можно положить в double-переменную — оно превратится само.", "let h: double = 1.68", "language/basics.md"),
            E("bool", ReferenceKind.Type, "bool", "Логическое значение: true или false. Результат сравнений; нужен для if и while.", "let ok: bool = 3 > 2", "language/basics.md"),
            E("string", ReferenceKind.Type, "string", "Строка текста в кавычках. Индексы, длина и срезы считаются в символах, поэтому русские буквы работают правильно. Склеивается через +, повторяется через *.", "let s = \"Привет\"", "language/strings.md"),

            // ---------------------------------------------------------------- значения
            E("true", ReferenceKind.Literal, "true", "Логическое «истина» (тип bool).", null, "language/basics.md"),
            E("false", ReferenceKind.Literal, "false", "Логическое «ложь» (тип bool).", null, "language/basics.md"),
            E("null", ReferenceKind.Literal, "null", "Пустая ссылка — «объекта нет». Допустима для объектов и массивов; обращение к полю или методу через null — ошибка выполнения.", "let p: Point = null", "language/objects.md"),

            // ---------------------------------------------------------------- интроспекция
            E("~layout", ReferenceKind.Introspection, "объект~layout", "Раскладка полей объекта в памяти: имя поля, номер слота (@0, @1 — смещение) и размер. Унаследованные поля идут первыми.", "print(d~layout)\n// layout(Dog): name@0 (8 байт), age@1 (8 байт)", "language/introspection.md"),
            E("~vtable", ReferenceKind.Introspection, "объект~vtable", "Таблица виртуальных методов класса объекта: для каждого метода — чья реализация реально выполнится (свой класс или унаследованная от предка).", "print(d~vtable)\n// vtable(Dog): eat -> Animal.eat, speak -> Dog.speak", "language/introspection.md"),
            E("~address", ReferenceKind.Introspection, "объект~address", "Адрес объекта в куче (число). При каждом запуске разный; одинаковый адрес — один и тот же объект.", "print(d~address)", "language/introspection.md"),
            E("~heap", ReferenceKind.Introspection, "~heap", "Снимок кучи: сколько объектов живо и какие. Не требует объекта.", "print(~heap)\n// heap(2 объект(ов)): Box@..., Box@...", "language/introspection.md"),
            E("~gc", ReferenceKind.Introspection, "~gc", "Запускает сборку мусора немедленно: недостижимые объекты освобождаются. Не требует объекта. В IDE то же делает кнопка «Собрать мусор».", "a = b\n~gc", "language/introspection.md"),

            // ---------------------------------------------------------------- константы
            E("PI", ReferenceKind.Constant, "PI", "Число π ≈ 3.14159265358979 (double).", "let area = PI * r * r", "reference/language-reference.md"),
            E("E", ReferenceKind.Constant, "E", "Число e ≈ 2.71828182845905 (double).", null, "reference/language-reference.md"),

            // ---------------------------------------------------------------- встроенные функции
            E("print", ReferenceKind.Function, "print(значения...)", "Печатает любое число значений любых типов через пробел и переходит на новую строку. print() без аргументов печатает пустую строку. Объект без toString() печатается как Класс@0x…, с toString() — его результатом.", "print(\"x =\", 5, true)  // x = 5 true", "reference/language-reference.md"),
            E("len", ReferenceKind.Function, "len(x) -> int", "Длина массива (число элементов) или строки (число символов). То же, что x.length.", "len([1, 2, 3])  // 3\nlen(\"Привет\")  // 6", "language/arrays.md"),
            E("str", ReferenceKind.Function, "str(x) -> string", "Превращает значение любого типа в строку так же, как print его показывает. Синоним: string(x).", "str(5) + \"!\"  // \"5!\"", "language/basics.md"),
            E("string", ReferenceKind.Function, "string(x) -> string", "То же, что str(x): значение любого типа → строка.", null, "language/basics.md"),
            E("int", ReferenceKind.Function, "int(x) -> int", "Преобразует в целое: из строки (\"42\"), из double (дробная часть отбрасывается к нулю), из bool. Непригодная строка — ошибка выполнения.", "int(\"42\")  // 42\nint(3.99)  // 3", "language/basics.md"),
            E("double", ReferenceKind.Function, "double(x) -> double", "Преобразует в число с дробной частью: из int, bool, строки (\"2.5\").", "double(7) / 2  // 3.5", "language/basics.md"),
            E("bool", ReferenceKind.Function, "bool(x) -> bool", "Преобразует в bool: число не равно нулю → true; строки \"true\" и \"false\" (регистр не важен).", "bool(0)  // false", "language/basics.md"),
            E("range", ReferenceKind.Function, "range(стоп) | range(старт, стоп) | range(старт, стоп, шаг) -> [int]", "Последовательность целых чисел (правая граница не входит). Шаг может быть отрицательным, но не нулём. В цикле for массив не создаётся.", "for i in range(2, 10, 3) {\n    print(i)   // 2 5 8\n}", "language/control-flow.md"),
            E("abs", ReferenceKind.Function, "abs(x)", "Модуль числа; тип результата — тот же, что у аргумента.", "abs(-3)  // 3", "reference/language-reference.md"),
            E("min", ReferenceKind.Function, "min(a, b) | min(массив)", "Наименьшее из двух чисел или из элементов массива чисел. Для пустого массива — ошибка.", "min(3, 5)  // 3\nmin([4, 2, 9])  // 2", "reference/language-reference.md"),
            E("max", ReferenceKind.Function, "max(a, b) | max(массив)", "Наибольшее из двух чисел или из элементов массива чисел. Для пустого массива — ошибка.", "max(3, 2.5)  // 3.0", "reference/language-reference.md"),
            E("sum", ReferenceKind.Function, "sum(массив чисел)", "Сумма элементов массива: int для [int], double для [double]. Сумма пустого массива — 0.", "sum([1, 2, 3])  // 6", "language/arrays.md"),
            E("sqrt", ReferenceKind.Function, "sqrt(x) -> double", "Квадратный корень. Для отрицательного числа — ошибка выполнения.", "sqrt(16)  // 4.0", "reference/language-reference.md"),
            E("pow", ReferenceKind.Function, "pow(основание, степень) -> double", "Возведение в степень; результат всегда double.", "pow(2, 10)  // 1024.0", "reference/language-reference.md"),
            E("floor", ReferenceKind.Function, "floor(x) -> int", "Округление вниз до целого.", "floor(2.7)  // 2", "reference/language-reference.md"),
            E("ceil", ReferenceKind.Function, "ceil(x) -> int", "Округление вверх до целого.", "ceil(2.1)  // 3", "reference/language-reference.md"),
            E("round", ReferenceKind.Function, "round(x) -> int", "Округление до ближайшего целого; половина — от нуля: round(2.5) == 3, round(-2.5) == -3.", "round(2.5)  // 3", "reference/language-reference.md"),

            // ---------------------------------------------------------------- свойство length
            E("length", ReferenceKind.Property, "x.length -> int", "Длина массива (число элементов) или строки (число символов). Это свойство — пишется без скобок.", "[1, 2, 3].length  // 3", "language/arrays.md"),

            // ---------------------------------------------------------------- методы массивов
            E("append", ReferenceKind.ArrayMethod, "xs.append(x)", "Добавляет элемент в конец массива. Тип x должен подходить к типу элементов.", "xs.append(4)", "language/arrays.md"),
            E("pop", ReferenceKind.ArrayMethod, "xs.pop() | xs.pop(i) -> элемент", "Убирает и возвращает последний элемент (или элемент с индексом i; отрицательный — от конца). Из пустого массива — ошибка.", "let last = xs.pop()", "language/arrays.md"),
            E("insert", ReferenceKind.ArrayMethod, "xs.insert(i, x)", "Вставляет x на место i; остальные сдвигаются. Индекс за границами «прижимается» к краю.", "xs.insert(0, 99)", "language/arrays.md"),
            E("remove", ReferenceKind.ArrayMethod, "xs.remove(x)", "Убирает первый элемент, равный x. Если такого нет — ошибка выполнения.", "xs.remove(5)", "language/arrays.md"),
            E("contains", ReferenceKind.ArrayMethod, "xs.contains(x) -> bool", "Есть ли элемент в массиве.", "xs.contains(3)", "language/arrays.md"),
            E("indexOf", ReferenceKind.ArrayMethod, "xs.indexOf(x) -> int", "Индекс первого элемента, равного x, или -1, если такого нет.", "xs.indexOf(8)", "language/arrays.md"),
            E("reverse", ReferenceKind.ArrayMethod, "xs.reverse()", "Разворачивает массив на месте. Чтобы получить развёрнутую копию, используйте срез xs[::-1].", "xs.reverse()", "language/arrays.md"),
            E("sort", ReferenceKind.ArrayMethod, "xs.sort()", "Сортирует массив на месте по возрастанию (числа или строки). Сортировка устойчивая.", "xs.sort()", "language/arrays.md"),
            E("clear", ReferenceKind.ArrayMethod, "xs.clear()", "Удаляет все элементы.", "xs.clear()", "language/arrays.md"),
            E("join", ReferenceKind.ArrayMethod, "xs.join(разделитель) -> string", "Склеивает элементы в одну строку через разделитель (числа превращаются в текст).", "[1, 2, 3].join(\"-\")  // \"1-2-3\"", "language/arrays.md"),

            // ---------------------------------------------------------------- методы строк
            E("upper", ReferenceKind.StringMethod, "s.upper() -> string", "Строка заглавными буквами (латиница, кириллица, греческий).", "\"привет\".upper()  // \"ПРИВЕТ\"", "language/strings.md"),
            E("lower", ReferenceKind.StringMethod, "s.lower() -> string", "Строка строчными буквами.", null, "language/strings.md"),
            E("trim", ReferenceKind.StringMethod, "s.trim() -> string", "Убирает пробелы и переводы строк по краям.", "\"  hi \".trim()  // \"hi\"", "language/strings.md"),
            E("contains", ReferenceKind.StringMethod, "s.contains(часть) -> bool", "Встречается ли подстрока в строке.", "\"hello\".contains(\"ell\")  // true", "language/strings.md"),
            E("startsWith", ReferenceKind.StringMethod, "s.startsWith(начало) -> bool", "Начинается ли строка с указанного текста.", null, "language/strings.md"),
            E("endsWith", ReferenceKind.StringMethod, "s.endsWith(конец) -> bool", "Заканчивается ли строка указанным текстом.", null, "language/strings.md"),
            E("indexOf", ReferenceKind.StringMethod, "s.indexOf(часть) -> int", "Номер символа, с которого начинается первое вхождение подстроки, или -1.", "\"привет\".indexOf(\"в\")  // 3", "language/strings.md"),
            E("replace", ReferenceKind.StringMethod, "s.replace(что, на_что) -> string", "Заменяет все вхождения подстроки. Строки неизменяемы: метод возвращает новую строку.", "\"мама\".replace(\"м\", \"р\")  // \"рара\"", "language/strings.md"),
            E("split", ReferenceKind.StringMethod, "s.split(разделитель) -> [string]", "Разбивает строку на массив строк. Пустой разделитель — на отдельные символы.", "\"a,b,c\".split(\",\")  // [\"a\", \"b\", \"c\"]", "language/strings.md"),
            E("repeat", ReferenceKind.StringMethod, "s.repeat(n) -> string", "Повторяет строку n раз (то же, что s * n).", "\"ха\".repeat(3)  // \"хахаха\"", "language/strings.md"),
        };

        /// <summary>Все записи с таким именем (например, contains — у массива и у строки).</summary>
        public static IReadOnlyList<ReferenceEntry> Find(string name) => All.Where(e => e.Name == name).ToList();

        public static IReadOnlyList<ReferenceEntry> OfKind(ReferenceKind kind) => All.Where(e => e.Kind == kind).ToList();
    }
}
