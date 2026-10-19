// Каталог встроенных примеров: те же examples/*.glass, что гоняются и в C++-
// тестах/glassc, встроены в сборку как ресурсы (см. Glass.Studio.Core.csproj).
// Порядок — учебный: от простого к «видно устройство памяти».

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;

namespace Glass.Studio.Examples
{
    public sealed record ExampleItem(string Id, string Title, string Description);

    public static class ExampleCatalog
    {
        public static IReadOnlyList<ExampleItem> All { get; } = new[]
        {
            new ExampleItem("hello", "Привет, Glass", "Полный пример языка: mold, facet, функции, циклы"),
            new ExampleItem("conversions", "Преобразование типов", "int(), double(), str(), print с несколькими значениями"),
            new ExampleItem("strings", "Строки", "Кириллица, срезы, split, upper, replace"),
            new ExampleItem("lists", "Массивы и срезы", "append, sort, срезы, отрицательные индексы, range"),
            new ExampleItem("factorial", "Рекурсия и циклы", "Факториал, числа Фибоначчи: стек вызовов при шаге F11"),
            new ExampleItem("arrays", "Массивы", "Литералы, индексация, for..in, массив объектов"),
            new ExampleItem("shapes", "Виртуальный вызов (facet)", "Один s.area() — разные классы: смотрите vtable в инспекторе"),
            new ExampleItem("exceptions", "Исключения", "throw / catch / finally, catch по базовому типу"),
            new ExampleItem("linked_list", "Связный список", "Цепочка объектов в куче: стрелки от head через все узлы"),
            new ExampleItem("gc_demo", "Сборка мусора", "Недостижимый объект краснеет, ~gc его освобождает"),
            new ExampleItem("introspection", "Интроспекция", "~address, ~layout, ~vtable, ~heap, ~gc прямо из программы"),
        };

        public static string LoadSource(string id)
        {
            Assembly assembly = typeof(ExampleCatalog).Assembly;
            string resource = "Examples." + id + ".glass";
            using Stream? stream = assembly.GetManifestResourceStream(resource);
            if (stream == null) throw new FileNotFoundException("Нет встроенного примера: " + id, resource);
            using var reader = new StreamReader(stream, System.Text.Encoding.UTF8);
            return reader.ReadToEnd();
        }

        /// <summary>Имена всех встроенных примеров — для проверки, что каталог ничего не забыл.</summary>
        public static IReadOnlyList<string> EmbeddedIds()
        {
            var ids = new List<string>();
            foreach (string name in typeof(ExampleCatalog).Assembly.GetManifestResourceNames())
            {
                if (name.StartsWith("Examples.", StringComparison.Ordinal) && name.EndsWith(".glass", StringComparison.Ordinal))
                {
                    ids.Add(name.Substring("Examples.".Length, name.Length - "Examples.".Length - ".glass".Length));
                }
            }
            ids.Sort(StringComparer.Ordinal);
            return ids;
        }
    }
}
