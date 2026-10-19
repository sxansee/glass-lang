using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Glass.Studio.Tests
{
    /// <summary>Каркас в духе tests/test_framework.h: проверки не прерывают тест, итог — число проваленных.</summary>
    public static class TestKit
    {
        private static readonly List<(string Name, Func<Task> Body)> Tests = new List<(string, Func<Task>)>();
        private static int _checks;
        private static int _failures;
        private static string _currentTest = "";

        public static void Add(string name, Action body) => Tests.Add((name, () => { body(); return Task.CompletedTask; }));

        public static void Add(string name, Func<Task> body) => Tests.Add((name, body));

        public static void Check(bool condition, string description)
        {
            _checks++;
            if (condition) return;
            _failures++;
            Console.WriteLine("FAILED [" + _currentTest + "]: " + description);
        }

        public static void CheckEqual<T>(T expected, T actual, string description)
        {
            _checks++;
            if (EqualityComparer<T>.Default.Equals(expected, actual)) return;
            _failures++;
            Console.WriteLine("FAILED [" + _currentTest + "]: " + description);
            Console.WriteLine("  ожидалось: " + (expected?.ToString() ?? "null"));
            Console.WriteLine("  получено:  " + (actual?.ToString() ?? "null"));
        }

        public static void CheckContains(string haystack, string needle, string description)
        {
            _checks++;
            if (haystack.Contains(needle, StringComparison.Ordinal)) return;
            _failures++;
            Console.WriteLine("FAILED [" + _currentTest + "]: " + description);
            Console.WriteLine("  ожидалось вхождение: " + needle);
            Console.WriteLine("  в тексте:            " + haystack);
        }

        public static void CheckThrows<TException>(Action action, string description) where TException : Exception
        {
            _checks++;
            try
            {
                action();
            }
            catch (TException)
            {
                return;
            }
            catch (Exception ex)
            {
                _failures++;
                Console.WriteLine("FAILED [" + _currentTest + "]: " + description + " — вместо " + typeof(TException).Name +
                                  " брошено " + ex.GetType().Name + ": " + ex.Message);
                return;
            }
            _failures++;
            Console.WriteLine("FAILED [" + _currentTest + "]: " + description + " — исключение не брошено");
        }

        public static async Task<int> RunAllAsync(string? filter)
        {
            foreach ((string name, Func<Task> body) in Tests)
            {
                if (filter != null && !name.Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;
                _currentTest = name;
                try
                {
                    // Каждый тест обязан уложиться в 60 с: зависший (например, не остановившийся
                    // цикл) не должен вешать сборку навсегда.
                    Task task = Task.Run(body);
                    if (await Task.WhenAny(task, Task.Delay(TimeSpan.FromSeconds(60))) != task)
                    {
                        _checks++;
                        _failures++;
                        Console.WriteLine("FAILED [" + name + "]: тест не завершился за 60 с — принудительный выход");
                        Console.WriteLine(_checks + " проверок, " + _failures + " провалено.");
                        return 2;
                    }
                    await task;
                }
                catch (Exception ex)
                {
                    _checks++;
                    _failures++;
                    Console.WriteLine("FAILED [" + name + "]: необработанное исключение " + ex.GetType().Name + ": " + ex.Message);
                    Console.WriteLine(ex.StackTrace);
                }
            }
            Console.WriteLine(_checks + " проверок, " + _failures + " провалено.");
            return _failures == 0 ? 0 : 1;
        }
    }
}
