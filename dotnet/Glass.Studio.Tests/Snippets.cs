using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Glass.Interop;
using Glass.Studio.Course;
using Glass.Studio.Debugging;

namespace Glass.Studio.Tests
{
    /// <summary>
    /// Запуск примеров из документации и курса. Фрагмент без fn main дополняется до программы:
    /// если в нём есть объявления (fn/mold/facet) — добавляется пустой main, иначе всё оборачивается в main.
    /// </summary>
    public static class Snippets
    {
        public static string ToProgram(string code)
        {
            if (code.Contains("fn main(", StringComparison.Ordinal)) return code;
            bool hasDeclarations = code.Replace("\r\n", "\n").Split('\n').Any(l =>
                l.StartsWith("fn ", StringComparison.Ordinal) || l.StartsWith("mold ", StringComparison.Ordinal) || l.StartsWith("facet ", StringComparison.Ordinal));
            return hasDeclarations ? code + "\nfn main() {\n}\n" : "fn main() {\n" + code + "\n}\n";
        }

        public sealed record Outcome(string? Error, IReadOnlyList<string> Output);

        public static async Task<Outcome> RunAsync(string code)
        {
            string program = ToProgram(code);
            DebugSession session;
            try
            {
                session = DebugSession.Compile(program);
            }
            catch (GlassCompileException ex)
            {
                return new Outcome("компиляция: " + ex.Message, Array.Empty<string>());
            }
            using (session)
            {
                Task<CommandResult> run = session.RunAsync();
                if (await Task.WhenAny(run, Task.Delay(10000)) != run)
                {
                    session.RequestStop();
                    await run;
                    return new Outcome("не завершилась за 10 с", Array.Empty<string>());
                }
                CommandResult result = await run;
                return new Outcome(result.RuntimeError == null ? null : "выполнение: " + result.RuntimeError, LessonChecker.Lines(session.GetOutput()));
            }
        }

        /// <summary>Пары «блок glass → следующий за ним блок output»: (код, ожидаемый вывод или null).</summary>
        public static IEnumerable<(string Code, IReadOnlyList<string>? Expected)> Examples(IReadOnlyList<ContentBlock> blocks)
        {
            for (int i = 0; i < blocks.Count; i++)
            {
                if (blocks[i] is CodeBlock code && code.Language == "glass")
                {
                    IReadOnlyList<string>? expected = null;
                    if (i + 1 < blocks.Count && blocks[i + 1] is CodeBlock next && next.IsOutput) expected = LessonChecker.Lines(next.Code);
                    yield return (code.Code, expected);
                }
            }
        }
    }
}
