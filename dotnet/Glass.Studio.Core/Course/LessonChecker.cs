// Проверка решения ученика: компиляция → выполнение (с таймаутом) → сравнение вывода → обязательные приёмы.
// Выполняется настоящей VM (через DebugSession), поэтому ученик видит ровно то, что видел бы в IDE.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Glass.Interop;
using Glass.Studio.Debugging;
using Glass.Studio.Models;

namespace Glass.Studio.Course
{
    public enum CheckStatus { Passed, CompileError, RuntimeError, Timeout, WrongOutput, MissingFragment }

    public sealed record CheckResult(
        CheckStatus Status,
        string Message,
        IReadOnlyList<string> ActualOutput,
        IReadOnlyList<string> ExpectedOutput,
        Diagnostic? Diagnostic)
    {
        public bool Passed => Status == CheckStatus.Passed;
    }

    public static class LessonChecker
    {
        public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(5);

        public static async Task<CheckResult> CheckAsync(Lesson lesson, string source, TimeSpan? timeout = null)
        {
            IReadOnlyList<string> expected = lesson.ExpectedOutput;

            DebugSession session;
            try
            {
                session = DebugSession.Compile(source);
            }
            catch (GlassCompileException ex)
            {
                Diagnostic diagnostic = DiagnosticParser.ParseCompileError(ex.Message);
                string where = diagnostic.Line > 0 ? "Строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) + ": " : "";
                return new CheckResult(CheckStatus.CompileError, where + diagnostic.Message, Array.Empty<string>(), expected, diagnostic);
            }

            using (session)
            {
                Task<CommandResult> run = session.RunAsync();
                Task finished = await Task.WhenAny(run, Task.Delay(timeout ?? DefaultTimeout)).ConfigureAwait(false);
                if (finished != run)
                {
                    session.RequestStop();
                    await run.ConfigureAwait(false);
                    return new CheckResult(CheckStatus.Timeout,
                        "Программа выполняется слишком долго — похоже на бесконечный цикл. Проверьте условие выхода из цикла.",
                        Lines(session.GetOutput()), expected, null);
                }

                CommandResult result = await run.ConfigureAwait(false);
                IReadOnlyList<string> actual = Lines(session.GetOutput());
                if (result.RuntimeError != null)
                {
                    Diagnostic diagnostic = DiagnosticParser.ParseRuntimeError(result.RuntimeError);
                    string where = diagnostic.Line > 0 ? "Строка " + diagnostic.Line.ToString(CultureInfo.InvariantCulture) + ": " : "";
                    return new CheckResult(CheckStatus.RuntimeError, "Программа упала. " + where + diagnostic.Message, actual, expected, diagnostic);
                }

                string? mismatch = CompareOutput(expected, actual);
                if (mismatch != null) return new CheckResult(CheckStatus.WrongOutput, mismatch, actual, expected, null);

                foreach (string fragment in lesson.RequiredFragments)
                {
                    if (!source.Contains(fragment, StringComparison.Ordinal))
                    {
                        return new CheckResult(CheckStatus.MissingFragment,
                            "Вывод верный, но задание просит использовать «" + fragment + "» — добавьте это в решение.",
                            actual, expected, null);
                    }
                }
                return new CheckResult(CheckStatus.Passed, "Всё верно!", actual, expected, null);
            }
        }

        public static IReadOnlyList<string> Lines(string output)
        {
            var lines = output.Replace("\r\n", "\n").Split('\n').Select(l => l.TrimEnd()).ToList();
            while (lines.Count > 0 && lines[lines.Count - 1].Length == 0) lines.RemoveAt(lines.Count - 1);
            return lines;
        }

        /// <summary>null — вывод совпал; иначе понятное сообщение о первом расхождении.</summary>
        public static string? CompareOutput(IReadOnlyList<string> expected, IReadOnlyList<string> actual)
        {
            int common = Math.Min(expected.Count, actual.Count);
            for (int i = 0; i < common; i++)
            {
                if (!LineMatches(expected[i], actual[i]))
                {
                    string want = expected[i].StartsWith("re:", StringComparison.Ordinal) ? "строка по шаблону " + expected[i].Substring(3) : "«" + expected[i] + "»";
                    return "Строка " + (i + 1).ToString(CultureInfo.InvariantCulture) + " вывода: ожидалось " + want + ", а напечатано «" + actual[i] + "».";
                }
            }
            if (actual.Count < expected.Count)
            {
                return actual.Count == 0
                    ? "Программа ничего не напечатала, а ожидалось строк: " + expected.Count.ToString(CultureInfo.InvariantCulture) + "."
                    : "Выведено строк: " + actual.Count.ToString(CultureInfo.InvariantCulture) + ", а ожидалось: " + expected.Count.ToString(CultureInfo.InvariantCulture) + ".";
            }
            if (actual.Count > expected.Count)
            {
                return "Лишний вывод: строка " + (expected.Count + 1).ToString(CultureInfo.InvariantCulture) + " «" + actual[expected.Count] + "» не ожидалась.";
            }
            return null;
        }

        private static bool LineMatches(string expected, string actual) =>
            expected.StartsWith("re:", StringComparison.Ordinal)
                ? Regex.IsMatch(actual, expected.Substring(3))
                : expected == actual;
    }
}
