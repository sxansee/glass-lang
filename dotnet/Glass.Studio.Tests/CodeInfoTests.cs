using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Glass.Studio.CodeInfo;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    public static class CodeInfoTests
    {
        public static void Register()
        {
            Add("подсказки: сканер лексем", Scanner);
            Add("подсказки: справочник покрывает весь язык (лексер и таблица встроенных)", ReferenceCoverage);
            Add("подсказки: карточки элементов программы (переменные, поля, методы, mold)", ProgramElements);
            Add("подсказки: встроенные функции, методы и литералы", BuiltinElements);
            Add("подсказки: крайние случаи и карточка-ViewModel", EdgeCases);
        }

        private const string Sample = """
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
    let age: int

    Dog(n: string, a: int) {
        super(n)
        age = a
    }

    override fn speak() -> string {
        return this.name
    }
}

fn twice(x: int) -> int {
    return x * 2
}

fn main() {
    let d = Dog("Рекс", 3)
    let xs = [1, 2, 3]
    let total = twice(5)
    let r = 2.5
    let s = "привет"
    for i in range(3) {
        print(i)
    }
    print(d.speak(), d.age, xs.length, s.upper())
    xs.append(4)
    xs.contains(2)
    s.contains("в")
    print(d~layout)
    // комментарий
    print(unknownName)
}
""";

        private static int At(string source, string marker, int occurrence = 1, int shift = 0)
        {
            int index = -1;
            for (int i = 0; i < occurrence; i++) index = source.IndexOf(marker, index + 1, StringComparison.Ordinal);
            if (index < 0) throw new InvalidOperationException("в образце нет «" + marker + "» (вхождение " + occurrence + ")");
            return index + shift;
        }

        private static ElementInfo? Info(string marker, int occurrence = 1, int shift = 0) => CodeInfoProvider.Describe(Sample, At(Sample, marker, occurrence, shift));

        // ------------------------------------------------------------------ сканер

        private static void Scanner()
        {
            string code = "let x = 0..5 // хвост\nprint(\"а\\\"б\", 2.5e3, ~layout, a >= b)\nlet s = \"не закрыто";
            IReadOnlyList<SourceToken> tokens = SourceScanner.Scan(code);
            string Text(int i) => tokens[i].Text(code);

            CheckEqual(TokenKind.Keyword, tokens[0].Kind, "let — ключевое слово");
            CheckEqual(TokenKind.Identifier, tokens[1].Kind, "x — идентификатор");
            CheckEqual("0", Text(3), "0..5: число 0");
            CheckEqual("..", Text(4), "0..5: диапазон не склеился в дробь");
            CheckEqual("5", Text(5), "0..5: число 5");
            CheckEqual(TokenKind.Comment, tokens[6].Kind, "комментарий");
            CheckEqual("// хвост", Text(6), "текст комментария до конца строки");
            CheckEqual(1, tokens[0].Line, "строка первой лексемы");
            CheckEqual(1, tokens[0].Column, "столбец первой лексемы");
            CheckEqual(2, tokens[7].Line, "print — вторая строка");
            SourceToken str = tokens.First(t => t.Kind == TokenKind.String);
            CheckEqual("\"а\\\"б\"", str.Text(code), "строка с экранированной кавычкой — одна лексема");
            CheckEqual("2.5e3", tokens.First(t => t.Kind == TokenKind.Number && t.Text(code).Contains('e')).Text(code), "число с экспонентой");
            CheckEqual("~layout", tokens.First(t => t.Kind == TokenKind.Introspection).Text(code), "~layout — одна лексема");
            CheckEqual(">=", tokens.First(t => t.Kind == TokenKind.Operator && t.Text(code).Length == 2 && t.Text(code) == ">=").Text(code), "двухсимвольный оператор");
            SourceToken open = tokens.Last(t => t.Kind == TokenKind.String);
            CheckEqual("\"не закрыто", open.Text(code), "незакрытая строка доходит до конца текста");

            CheckEqual(0, SourceScanner.Scan("").Count, "пустой текст — нет лексем");
            CheckEqual(TokenKind.Identifier, SourceScanner.Scan("привет_мир1")[0].Kind, "кириллица и подчёркивание в именах");
            CheckEqual(TokenKind.Operator, SourceScanner.Scan("~foo")[0].Kind, "~ без слова интроспекции — обычный оператор");
        }

        // ------------------------------------------------------------------ полнота справочника

        private static string? FindRepoFile(string relative)
        {
            for (DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, relative);
                if (File.Exists(candidate)) return candidate;
            }
            return null;
        }

        private static void ReferenceCoverage()
        {
            // Устройство самого справочника.
            foreach (ReferenceEntry e in LanguageReference.All)
            {
                Check(e.Description.Length > 15 && e.Signature.Length > 0 && e.DocTopic.EndsWith(".md", StringComparison.Ordinal), e.Name + ": запись заполнена");
            }
            Check(LanguageReference.All.GroupBy(e => (e.Name, e.Kind)).All(g => g.Count() == 1), "пары (имя, вид) уникальны");
            Check(LanguageReference.All.Count >= 70, "записей в справочнике: " + LanguageReference.All.Count);

            // Ключевые слова лексера = сканер = справочник.
            string? lexerPath = FindRepoFile(Path.Combine("src", "lexer", "Lexer.cpp"));
            string? buildersPath = FindRepoFile(Path.Combine("src", "bytecode", "Builtins.h"));
            if (lexerPath == null || buildersPath == null)
            {
                Console.WriteLine("  (сверка с исходниками C++ пропущена: каталог src не найден)");
                return;
            }

            string lexer = File.ReadAllText(lexerPath);
            int start = lexer.IndexOf("std::unordered_map<std::string, TokenType> table", StringComparison.Ordinal);
            int end = lexer.IndexOf("};", start, StringComparison.Ordinal);
            var lexerKeywords = Regex.Matches(lexer.Substring(start, end - start), "\\{\"(\\w+)\",\\s*TokenType::").Select(m => m.Groups[1].Value).ToHashSet();
            var introspection = SourceScanner.IntrospectionWords;
            var lexerPlain = lexerKeywords.Where(k => !introspection.Contains(k)).ToHashSet();

            CheckEqual(string.Join(",", lexerPlain.OrderBy(x => x, StringComparer.Ordinal)),
                       string.Join(",", SourceScanner.Keywords.OrderBy(x => x, StringComparer.Ordinal)), "ключевые слова сканера = ключевые слова лексера");
            foreach (string keyword in lexerPlain)
            {
                Check(LanguageReference.Find(keyword).Any(e => e.Kind is ReferenceKind.Keyword or ReferenceKind.Modifier or ReferenceKind.Type or ReferenceKind.Literal),
                    "в справочнике нет ключевого слова «" + keyword + "»");
            }
            foreach (string word in introspection)
            {
                Check(LanguageReference.Find("~" + word).Any(e => e.Kind == ReferenceKind.Introspection), "в справочнике нет оператора ~" + word);
                Check(lexerKeywords.Contains(word), "лексер знает слово интроспекции «" + word + "»");
            }

            // Таблица встроенных функций и методов (Builtins.h) = справочник.
            string builtins = File.ReadAllText(buildersPath);
            void CheckTable(string function, ReferenceKind kind)
            {
                int from = builtins.IndexOf(function, StringComparison.Ordinal);
                int to = builtins.IndexOf("};", from, StringComparison.Ordinal);
                var names = Regex.Matches(builtins.Substring(from, to - from), "\\{\"(\\w+)\",\\s*Builtin::").Select(m => m.Groups[1].Value).ToList();
                Check(names.Count >= 10, function + ": имён в таблице " + names.Count);
                foreach (string name in names)
                {
                    Check(LanguageReference.Find(name).Any(e => e.Kind == kind), "в справочнике нет " + kind + " «" + name + "» (есть в Builtins.h)");
                }
                foreach (ReferenceEntry e in LanguageReference.OfKind(kind))
                {
                    Check(names.Contains(e.Name) || e.Name == "print", "справочник описывает " + kind + " «" + e.Name + "», которого нет в Builtins.h");   // print разбирает сам типизатор
                }
            }
            CheckTable("findBuiltinFunction", ReferenceKind.Function);
            CheckTable("findArrayMethod", ReferenceKind.ArrayMethod);
            CheckTable("findStringMethod", ReferenceKind.StringMethod);
        }

        // ------------------------------------------------------------------ элементы программы

        private static void ProgramElements()
        {
            ElementInfo? fn = Info("fn", 1);
            Check(fn != null && fn.KindText == "ключевое слово" && fn.Title.StartsWith("fn имя", StringComparison.Ordinal), "fn — ключевое слово");

            ElementInfo? call = Info("twice(5)");
            Check(call != null, "вызов своей функции");
            CheckEqual("fn twice(x: int) -> int", call?.Title, "сигнатура функции пользователя");
            CheckEqual("функция", call?.KindText, "вид — функция");
            CheckContains(call?.Origin ?? "", "объявлено в строке", "откуда функция");
            CheckContains(call?.Description ?? "", "int", "возвращаемый тип назван");

            ElementInfo? variable = Info("d.speak", 1);
            CheckEqual("d: Dog", variable?.Title, "переменная d имеет тип Dog (из вызова конструктора)");
            CheckEqual("переменная", variable?.KindText, "вид — переменная");

            ElementInfo? method = Info("d.speak", 1, 2);   // d.speak() в main
            CheckEqual("метод", method?.KindText, "d.speak() — метод");
            CheckContains(method?.Title ?? "", "speak() -> string", "сигнатура метода");
            CheckContains(method?.Title ?? "", "override", "найден override из Dog, а не virtual из Animal");

            ElementInfo? declared = Info("override fn speak", 1, 12);
            CheckEqual("метод", declared?.KindText, "имя в объявлении метода");
            CheckContains(declared?.Title ?? "", "override fn speak() -> string", "сигнатура из объявления");

            ElementInfo? field = Info("d.age", 1, 2);
            CheckEqual("age: int", field?.Title, "поле age");
            CheckEqual("поле", field?.KindText, "вид — поле");
            CheckContains(field?.Description ?? "", "mold Dog", "чьё поле");

            ElementInfo? inherited = Info("this.name", 1, 5);
            CheckEqual("name: string", inherited?.Title, "унаследованное поле через this");

            ElementInfo? ctor = Info("Dog(\"Рекс\"");
            CheckEqual("конструктор", ctor?.KindText, "вызов Dog(...) — конструктор");
            CheckEqual("Dog(n: string, a: int)", ctor?.Title, "сигнатура конструктора");

            ElementInfo? mold = Info("Animal", 3);   // в «mold Dog : Animal»
            CheckEqual("mold", mold?.KindText, "имя предка — mold");
            CheckContains(mold?.Description ?? "", "Методы: speak()", "перечислены методы");
            CheckContains(mold?.Description ?? "", "Поля: name: string", "перечислены поля");
            ElementInfo? dogMold = Info("Dog", 1);
            CheckContains(dogMold?.Description ?? "", "Наследует Animal", "предок назван");

            CheckEqual("xs: [int]", Info("xs = [1")?.Title, "тип массива выведен по литералу");
            CheckEqual("total: int", Info("total = twice")?.Title, "тип выведен по возвращаемому типу функции");
            CheckEqual("r: double", Info("r = 2.5")?.Title, "double по литералу");
            CheckEqual("s: string", Info("s = \"привет\"")?.Title, "string по литералу");
            CheckContains(Info("total = twice")?.Description ?? "", "выведен из значения", "сказано, что тип выведен");

            ElementInfo? loop = Info("i in range");
            CheckEqual("i: int", loop?.Title, "переменная цикла по range — int");
            CheckEqual("переменная цикла", loop?.KindText, "вид — переменная цикла");

            ElementInfo? parameter = Info("x * 2");
            CheckEqual("x: int", parameter?.Title, "параметр");
            CheckEqual("параметр", parameter?.KindText, "вид — параметр");

            ElementInfo? unknown = Info("unknownName");
            CheckEqual("имя", unknown?.KindText, "неизвестное имя");
            CheckContains(unknown?.Description ?? "", "Не нашёл объявления", "понятная причина");

            // Свой max перекрывает встроенный (так же поступает компилятор).
            string shadow = "fn max(a: int, b: int) -> int {\n    return a\n}\nfn main() {\n    print(max(1, 2))\n}\n";
            ElementInfo? own = CodeInfoProvider.Describe(shadow, shadow.LastIndexOf("max", StringComparison.Ordinal));
            CheckEqual("функция", own?.KindText, "своя функция max важнее встроенной");
        }

        // ------------------------------------------------------------------ встроенное

        private static void BuiltinElements()
        {
            ElementInfo? print = Info("print(i)");
            CheckEqual("встроенная функция", print?.KindText, "print — встроенная функция");

            ElementInfo? intKeyword = CodeInfoProvider.Describe("let n: int = int(\"5\")", 7);
            CheckContains(intKeyword?.KindText ?? "", "тип данных", "int — тип");
            CheckContains(intKeyword?.KindText ?? "", "встроенная функция", "и функция преобразования");
            CheckContains(intKeyword?.Description ?? "", "Целое число", "описание типа");
            CheckContains(intKeyword?.Description ?? "", "Преобразует в целое", "описание функции");

            CheckEqual("метод массива", Info("append")?.KindText, "append — метод массива");
            CheckEqual("метод массива", Info("contains", 1)?.KindText, "contains у массива (получатель — массив)");
            CheckEqual("метод строки", Info("contains", 2)?.KindText, "contains у строки (получатель — строка)");
            CheckEqual("метод строки", Info("upper")?.KindText, "upper — метод строки");
            CheckEqual("свойство", Info("length")?.KindText, "length — свойство");

            ElementInfo? layout = Info("~layout");
            CheckEqual("оператор интроспекции", layout?.KindText, "~layout");
            Check(layout?.Example != null && layout.Example.Contains("layout(Dog)", StringComparison.Ordinal), "у ~layout есть пример");
            CheckEqual("doc", (layout?.DocUrl ?? "").Contains("language/introspection.md", StringComparison.Ordinal) ? "doc" : "нет", "ссылка на документацию");

            ElementInfo? number = Info("3)");
            CheckEqual("число int", number?.KindText, "целое число");
            CheckEqual("число double", Info("2.5")?.KindText, "дробное число");
            ElementInfo? text = Info("\"привет\"", 1, 2);
            CheckEqual("строка", text?.KindText, "строковый литерал");
            CheckContains(text?.Description ?? "", "6 символов", "длина в символах (кириллица считается правильно)");
            CheckEqual("Комментарий", Info("// комментарий", 1, 4)?.Title, "комментарий");

            CheckEqual("оператор", CodeInfoProvider.Describe("a + b", 2)?.KindText, "оператор +");
            CheckContains(CodeInfoProvider.Describe("a + b", 2)?.Description ?? "", "склейка", "про склейку строк");
            CheckContains(CodeInfoProvider.Describe("for i in 0..5 {}", 11)?.Description ?? "", "Диапазон", "оператор ..");
            CheckEqual("константа", "константа", "заглушка для порядка");
            CheckContains(CodeInfoProvider.Describe("let a = PI", 9)?.KindText ?? "", "константа", "PI — встроенная константа");
        }

        // ------------------------------------------------------------------ крайние случаи

        private static void EdgeCases()
        {
            Check(CodeInfoProvider.Describe("", 0) == null, "пустой текст");
            Check(CodeInfoProvider.Describe("let x = 5", -1) == null, "отрицательное смещение");
            Check(CodeInfoProvider.Describe("let x = 5", 100) == null, "смещение за концом текста");
            Check(CodeInfoProvider.Describe("let   x", 4) == null, "пробел между словами — подсказки нет");
            Check(CodeInfoProvider.Describe("a ; b", 2) == null, "оператор без описания");
            // Курсор сразу ЗА словом (конец строки) — берём слово слева.
            CheckEqual("let", CodeInfoProvider.Describe("let", 3)?.Title.Split(' ')[0], "конец текста сразу за словом");
            CheckEqual("fn", CodeInfoProvider.Describe("fn\nlet", 2)?.Title.Split(' ')[0], "конец строки сразу за словом");
            // Недописанный код не ломает подсказки.
            ElementInfo? broken = CodeInfoProvider.Describe("fn main( {\n    let x = \n    print(x", 30);
            Check(broken != null, "подсказка на недописанном коде");
            CheckEqual(0, CodeInfoProvider.Describe("let x = 5", 0)!.Start, "карточка знает границы элемента (начало)");
            CheckEqual(3, CodeInfoProvider.Describe("let x = 5", 0)!.Length, "карточка знает границы элемента (длина)");

            // ViewModel карточки.
            var card = new CodeInfoCardViewModel();
            Check(!card.HasExample && card.Title == "", "пустая карточка");
            Check(card.Show("let x = 5", 1), "Show возвращает true, когда подсказка есть");
            CheckContains(card.Title, "let", "заголовок");
            CheckEqual("ключевое слово", card.KindText, "вид");
            Check(card.HasExample && card.Example.Contains("let age", StringComparison.Ordinal), "пример");
            Check(card.HasOrigin && card.Origin == "встроено в язык", "происхождение");
            Check(card.HasDoc && card.DocUrl!.EndsWith("language/basics.md", StringComparison.Ordinal), "ссылка на документацию");
            Check(!card.Show("a ; b", 2), "Show возвращает false, когда подсказки нет");
            Check(card.Title == "" && !card.HasDoc, "после неудачного Show карточка пуста");
            int changed = 0;
            card.PropertyChanged += (_, _) => changed++;
            card.Show("while", 1);
            Check(changed >= 5, "ViewModel сообщает об изменениях (" + changed + ")");
            card.Clear();
            Check(card.Info == null, "Clear");
        }
    }
}
