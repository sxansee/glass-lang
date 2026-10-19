using System;
using System.Linq;
using System.Text.Json;
using Glass.Studio.Models;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    /// <summary>Чистые тесты разбора JSON/диагностик/форматирования — без нативной библиотеки.</summary>
    public static class JsonTests
    {
        public static void Register()
        {
            Add("json: стек вызовов", CallStack);
            Add("json: локальные переменные всех видов", Locals);
            Add("json: double всегда отличим от int", DoubleVersusInt);
            Add("json: куча — объекты, массивы, reachable", Heap);
            Add("json: информация об объекте и массиве, null", ObjectInfo);
            Add("json: мусор вместо JSON — исключение, а не тихая пустота", Garbage);
            Add("диагностики: компиляция и выполнение", Diagnostics);
            Add("форматирование: адреса, строки, склонения", Formatting);
        }

        private static void CallStack()
        {
            var frames = GlassJson.ParseCallStack("[{\"function\":\"Dog.speak\",\"line\":12},{\"function\":\"main\",\"line\":30}]");
            CheckEqual(2, frames.Count, "два фрейма");
            CheckEqual("Dog.speak", frames[0].Function, "имя метода с классом");
            CheckEqual(12, frames[0].Line, "строка верхнего фрейма");
            CheckEqual(0, GlassJson.ParseCallStack("[]").Count, "пустой стек");
        }

        private static void Locals()
        {
            var locals = GlassJson.ParseLocals(
                "[{\"name\":\"i\",\"value\":42},{\"name\":\"pi\",\"value\":3.14},{\"name\":\"ok\",\"value\":true}," +
                "{\"name\":\"s\",\"value\":\"a\\nb\"},{\"name\":\"n\",\"value\":null}," +
                "{\"name\":\"d\",\"value\":{\"ref\":\"1000\",\"kind\":\"object\",\"class\":\"Dog\"}}," +
                "{\"name\":\"xs\",\"value\":{\"ref\":\"2000\",\"kind\":\"array\",\"length\":3}}," +
                "{\"name\":\"big\",\"value\":{\"kind\":\"double\",\"text\":\"inf\"}}]");
            CheckEqual(8, locals.Count, "восемь переменных");
            CheckEqual(ValueKind.Int, locals[0].Value.Kind, "int");
            CheckEqual("42", locals[0].Value.Display, "значение int");
            CheckEqual(ValueKind.Double, locals[1].Value.Kind, "double");
            CheckEqual("3.14", locals[1].Value.Display, "значение double");
            CheckEqual(ValueKind.Bool, locals[2].Value.Kind, "bool");
            CheckEqual(ValueKind.String, locals[3].Value.Kind, "string");
            CheckEqual("\"a\\nb\"", locals[3].Value.Display, "перевод строки в значении экранируется для ячейки");
            CheckEqual(ValueKind.Null, locals[4].Value.Kind, "null");
            CheckEqual(ValueKind.Object, locals[5].Value.Kind, "объект");
            CheckEqual("1000", locals[5].Value.Address, "адрес объекта");
            CheckEqual("Dog", locals[5].Value.ClassName, "класс объекта");
            CheckEqual(ValueKind.Array, locals[6].Value.Kind, "массив");
            CheckEqual(3, locals[6].Value.Length, "длина массива");
            CheckEqual(ValueKind.Double, locals[7].Value.Kind, "inf — это double, а не мусор");
            CheckEqual("inf", locals[7].Value.Display, "текст inf");
            CheckEqual("int", new LocalItem(locals[0]).TypeText, "тип int в таблице");
            CheckEqual("Dog", new LocalItem(locals[5]).TypeText, "тип объекта в таблице");
            CheckEqual("array[3]", new LocalItem(locals[6]).TypeText, "тип массива в таблице");
        }

        private static void DoubleVersusInt()
        {
            // Нативная сторона обязана писать whole-double как 3.0 (см. test_double_json_is_distinguishable_from_int);
            // здесь — что C#-разбор действительно различает их.
            var locals = GlassJson.ParseLocals("[{\"name\":\"a\",\"value\":3.0},{\"name\":\"b\",\"value\":3},{\"name\":\"c\",\"value\":1e+20}]");
            CheckEqual(ValueKind.Double, locals[0].Value.Kind, "3.0 — double");
            CheckEqual(ValueKind.Int, locals[1].Value.Kind, "3 — int");
            CheckEqual(ValueKind.Double, locals[2].Value.Kind, "1e+20 — double");
        }

        private static void Heap()
        {
            HeapSnapshot heap = GlassJson.ParseHeap(
                "{\"objects\":[" +
                "{\"address\":\"10\",\"reachable\":true,\"kind\":\"object\",\"class\":\"Node\",\"fields\":[{\"name\":\"value\",\"value\":1},{\"name\":\"next\",\"value\":null}]}," +
                "{\"address\":\"20\",\"reachable\":false,\"kind\":\"array\",\"elements\":[5,{\"ref\":\"10\",\"kind\":\"object\",\"class\":\"Node\"}]}" +
                "]}");
            CheckEqual(2, heap.Objects.Count, "два объекта");
            HeapObject node = heap.Objects[0];
            CheckEqual("Node", node.ClassName, "класс");
            CheckEqual(false, node.IsArray, "объект, не массив");
            CheckEqual(true, node.Reachable, "достижим");
            CheckEqual("next", node.Members[1].Name, "имя поля");
            CheckEqual(ValueKind.Null, node.Members[1].Value.Kind, "next == null");
            HeapObject array = heap.Objects[1];
            CheckEqual(true, array.IsArray, "массив");
            CheckEqual(false, array.Reachable, "недостижим — мусор");
            CheckEqual("[1]", array.Members[1].Name, "имя элемента");
            CheckEqual("10", array.Members[1].Value.Address, "элемент-ссылка");
            // Старый формат без "reachable" считается достижимым — не красим всё красным.
            HeapSnapshot legacy = GlassJson.ParseHeap("{\"objects\":[{\"address\":\"1\",\"kind\":\"object\",\"class\":\"A\",\"fields\":[]}]}");
            CheckEqual(true, legacy.Objects[0].Reachable, "без поля reachable — достижим");
        }

        private static void ObjectInfo()
        {
            ObjectInfo? info = GlassJson.ParseObjectInfo(
                "{\"address\":\"77\",\"kind\":\"object\",\"class\":\"Dog\",\"ancestry\":[\"Dog\",\"Animal\"]," +
                "\"layout\":[{\"name\":\"name\",\"offset\":0,\"size\":8,\"type\":\"ref\"},{\"name\":\"age\",\"offset\":1,\"size\":8,\"type\":\"int\"}]," +
                "\"vtable\":[{\"method\":\"speak\",\"implementedBy\":\"Dog\"},{\"method\":\"eat\",\"implementedBy\":\"Animal\"}]}");
            Check(info != null, "объект разобран");
            if (info == null) return;
            CheckEqual("Dog", info.ClassName, "класс");
            CheckEqual(2, info.Ancestry.Count, "предки");
            CheckEqual("age", info.Layout[1].Name, "поле раскладки");
            CheckEqual(1, info.Layout[1].Offset, "смещение");
            CheckEqual("Animal", info.VTable[1].ImplementedBy, "унаследованная реализация");

            ObjectInfo? array = GlassJson.ParseObjectInfo("{\"address\":\"88\",\"kind\":\"array\",\"length\":4}");
            Check(array != null && array.IsArray && array.Length == 4, "массив разобран");
            Check(GlassJson.ParseObjectInfo("null") == null, "null — нет объекта");
            Check(GlassJson.ParseObjectInfo("") == null, "пусто — нет объекта");
            Check(GlassJson.ParseObjectInfo(null) == null, "null-строка — нет объекта");
        }

        private static void Garbage()
        {
            CheckThrows<JsonException>(() => GlassJson.ParseCallStack("не json"), "ParseCallStack: мусор");
            CheckThrows<JsonException>(() => GlassJson.ParseHeap("{\"objects\":"), "ParseHeap: оборванный JSON");
            CheckThrows<InvalidOperationException>(() => GlassJson.ParseLocals("{}"), "ParseLocals: не массив");
        }

        private static void Diagnostics()
        {
            Diagnostic compile = DiagnosticParser.ParseCompileError("2:18: тип 'string' не совместим с ожидаемым 'int'");
            CheckEqual(DiagnosticKind.Compile, compile.Kind, "вид: компиляция");
            CheckEqual(2, compile.Line, "строка");
            CheckEqual(18, compile.Column, "столбец");
            CheckEqual("тип 'string' не совместим с ожидаемым 'int'", compile.Message, "сообщение без позиции");

            Diagnostic runtime = DiagnosticParser.ParseRuntimeError("Dog.speak:12: индекс 5 вне границ массива (длина 2)");
            CheckEqual(DiagnosticKind.Runtime, runtime.Kind, "вид: выполнение");
            CheckEqual(12, runtime.Line, "строка из «функция:строка:»");
            CheckEqual("индекс 5 вне границ массива (длина 2)", runtime.Message, "сообщение");

            Diagnostic unknown = DiagnosticParser.ParseCompileError("что-то непонятное");
            CheckEqual(0, unknown.Line, "нераспознанное — без позиции");
            CheckEqual("что-то непонятное", unknown.Message, "но текст не теряется");

            var problem = new ProblemItem(compile);
            CheckEqual("строка 2, столбец 18", problem.LocationText, "позиция для списка ошибок");
            CheckEqual("", new ProblemItem(unknown).LocationText, "нет позиции — пустая строка");
        }

        private static void Formatting()
        {
            CheckEqual("0xff", AddressFormat.Hex("255"), "hex");
            CheckEqual("0x0", AddressFormat.Hex("0"), "hex нуля");
            CheckEqual("…b0b0", AddressFormat.Short("139874231234736"), "короткий адрес — 4 последние hex-цифры (0x7f3701ddb0b0)");
            CheckEqual("0x7f3701ddb0b0", AddressFormat.Hex("139874231234736"), "полный hex");
            CheckEqual("xyz", AddressFormat.Hex("xyz"), "не число — как есть");

            CheckEqual("\"abc\"", GlassValue.FromString("abc").Display, "строка в кавычках");
            string longText = GlassValue.FromString(new string('x', 100)).Display;
            Check(longText.Length < 40 && longText.EndsWith("…\"", StringComparison.Ordinal), "длинная строка усечена с многоточием");
            Check(!GlassValue.FromString("a\tb\r\nc").Display.Contains('\n'), "переводы строк не ломают ячейку");

            CheckEqual("объект", Plural.Choose(1, "объект", "объекта", "объектов"), "1");
            CheckEqual("объекта", Plural.Choose(2, "объект", "объекта", "объектов"), "2");
            CheckEqual("объектов", Plural.Choose(5, "объект", "объекта", "объектов"), "5");
            CheckEqual("объектов", Plural.Choose(11, "объект", "объекта", "объектов"), "11");
            CheckEqual("объект", Plural.Choose(21, "объект", "объекта", "объектов"), "21");
            CheckEqual("объекта", Plural.Choose(33, "объект", "объекта", "объектов"), "33");
            CheckEqual("объектов", Plural.Choose(112, "объект", "объекта", "объектов"), "112");
            CheckEqual("объектов", Plural.Choose(0, "объект", "объекта", "объектов"), "0");
        }
    }
}
