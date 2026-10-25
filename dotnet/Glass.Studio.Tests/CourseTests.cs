using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Glass.Interop;
using Glass.Studio.Course;
using Glass.Studio.Debugging;
using Glass.Studio.Logging;
using Glass.Studio.ViewModels;
using static Glass.Studio.Tests.TestKit;

namespace Glass.Studio.Tests
{
    public static class CourseTests
    {
        public static void Register()
        {
            Add("курс: мини-разметка текста уроков", Markup);
            Add("курс: звёзды и уровни", Scoring);
            Add("курс: структура каталога (6 глав, 24 урока, уникальные id, тексты)", CatalogStructure);
            Add("курс: каждый пример из теории выполняется и печатает показанное", TheoryExamples);
            Add("курс: решение каждого урока проходит проверку", SolutionsPass);
            Add("курс: заготовка каждого урока компилируется, но проверку НЕ проходит", StartersFail);
            Add("курс: проверка решений — все виды неудач", CheckerFailures);
            Add("курс: прогресс — сохранение, повреждённый файл", ProgressStore);
            Add("курс: окно курса — сценарий ученика целиком", ViewModelFlow);
            Add("курс: подсказки, решение, звёзды, навигация, сброс", ViewModelDetails);
            Add("курс: список уроков (своя команда у строки) и подпись кнопки «Начать/Продолжить/Повторить»", ListAndStartButton);
        }

        private sealed class FakeCourseHost : ICourseHost
        {
            public string Source = "";
            public string? LoadedTitle;
            public bool Accept = true;
            public int LoadCalls;

            public string GetSource() => Source;

            public bool LoadProgram(string text, string title)
            {
                LoadCalls++;
                if (!Accept) return false;
                Source = text;
                LoadedTitle = title;
                return true;
            }
        }

        private static string TempFile() => Path.Combine(Path.GetTempPath(), "glass-course-" + Guid.NewGuid().ToString("N") + ".json");

        // ------------------------------------------------------------------ разметка

        private static void Markup()
        {
            IReadOnlyList<ContentBlock> blocks = LessonMarkup.Parse(
                "# Заголовок\n\nПервая строка\nвторая строка.\n\n- пункт **один**\n- пункт `два`\n  продолжение\n\n> Заметка\n\n```glass\nfn main() {}\n```\n\n```output\nok\n```\n");
            CheckEqual(6, blocks.Count, "заголовок, абзац, список, заметка, код, вывод");
            Check(blocks[0] is HeadingBlock h && h.Text == "Заголовок", "заголовок");
            Check(blocks[1] is ParagraphBlock p && p.Text.PlainText == "Первая строка вторая строка.", "строки абзаца склеиваются пробелом");
            var bullets = (BulletBlock)blocks[2];
            CheckEqual(2, bullets.Items.Count, "два пункта");
            Check(bullets.Items[0].Spans.Any(s => s.IsBold && s.Text == "один"), "жирный в пункте");
            Check(bullets.Items[1].Spans.Any(s => s.IsCode && s.Text == "два"), "код в пункте");
            CheckEqual("пункт два продолжение", bullets.Items[1].PlainText, "продолжение пункта (с отступом) приклеивается");
            Check(blocks[3] is NoteBlock n && n.Text.PlainText == "Заметка", "примечание");
            var code = (CodeBlock)blocks[4];
            CheckEqual("glass", code.Language, "язык блока");
            CheckEqual("fn main() {}", code.Code, "код блока");
            Check(((CodeBlock)blocks[5]).IsOutput, "блок вывода");

            RichText inline = RichText.Parse("a **b** `c` d");
            CheckEqual(5, inline.Spans.Count, "обычный, жирный, обычный, код, обычный");
            CheckEqual("a b c d", inline.PlainText, "разметка не попадает в текст");
            CheckEqual("не **закрыто и `тоже", RichText.Parse("не **закрыто и `тоже").PlainText, "незакрытые маркеры остаются текстом");
            Check(RichText.Parse("`a * b ** c`").Spans[0].IsCode, "внутри кода звёздочки — просто символы");
            CheckEqual(0, LessonMarkup.Parse("").Count, "пусто — нет блоков");
        }

        private static void Scoring()
        {
            CheckEqual(3, CourseScoring.Stars(1, false, false), "с первой попытки");
            CheckEqual(3, CourseScoring.Stars(2, false, false), "со второй");
            CheckEqual(2, CourseScoring.Stars(3, false, false), "с третьей");
            CheckEqual(2, CourseScoring.Stars(1, true, false), "с подсказкой");
            CheckEqual(1, CourseScoring.Stars(1, false, true), "после просмотра решения");
            CheckEqual(1, CourseScoring.Stars(9, true, true), "решение важнее подсказки");
            CheckEqual("★★☆", CourseScoring.StarsText(2), "текст звёзд");
            CheckEqual("☆☆☆", CourseScoring.StarsText(0), "ноль звёзд");
            CheckEqual("Новичок", CourseScoring.LevelTitle(0, 24), "уровень 0");
            CheckEqual("Ученик", CourseScoring.LevelTitle(3, 24), "уровень 3");
            CheckEqual("Практик", CourseScoring.LevelTitle(8, 24), "уровень 8");
            CheckEqual("Знаток", CourseScoring.LevelTitle(14, 24), "уровень 14");
            CheckEqual("Мастер", CourseScoring.LevelTitle(20, 24), "уровень 20");
            CheckEqual("Архитектор Glass", CourseScoring.LevelTitle(24, 24), "все уроки");
            CheckEqual("Новичок", CourseScoring.LevelTitle(0, 0), "пустой курс — без деления на ноль");
        }

        // ------------------------------------------------------------------ каталог

        private static void CatalogStructure()
        {
            CheckEqual(6, CourseCatalog.Chapters.Count, "шесть глав");
            CheckEqual(24, CourseCatalog.AllLessons.Count, "24 урока");
            CheckEqual(24, CourseCatalog.AllLessons.Select(l => l.Id).Distinct().Count(), "id уроков уникальны");
            int expectedChapter = 1;
            foreach (Chapter chapter in CourseCatalog.Chapters)
            {
                CheckEqual(expectedChapter++, chapter.Number, "главы идут по порядку");
                Check(chapter.Title.Length > 0 && chapter.Subtitle.Length > 0 && chapter.Lessons.Count >= 3, "глава «" + chapter.Title + "» заполнена");
                for (int i = 0; i < chapter.Lessons.Count; i++)
                {
                    Lesson lesson = chapter.Lessons[i];
                    CheckEqual(i + 1, lesson.Number, "нумерация уроков главы " + chapter.Number + ": " + lesson.Id);
                    CheckEqual(chapter.Number, lesson.ChapterNumber, "урок " + lesson.Id + " в своей главе");
                    Check(lesson.Title.Length > 0 && lesson.Summary.Length > 0, lesson.Id + ": название и описание");
                    Check(lesson.Theory.Length > 150, lesson.Id + ": есть теория");
                    Check(lesson.Task.Length > 20, lesson.Id + ": есть задача");
                    Check(lesson.Starter.Contains("fn main", StringComparison.Ordinal), lesson.Id + ": в заготовке есть main");
                    Check(lesson.Solution.Contains("fn main", StringComparison.Ordinal), lesson.Id + ": в решении есть main");
                    Check(lesson.Hint.Length > 10, lesson.Id + ": есть подсказка");
                    Check(lesson.ExpectedOutput.Count > 0, lesson.Id + ": задан ожидаемый вывод");
                    Check(!string.Equals(lesson.Starter, lesson.Solution, StringComparison.Ordinal), lesson.Id + ": заготовка отличается от решения");
                    foreach (string line in lesson.ExpectedOutput.Where(l => l.StartsWith("re:", StringComparison.Ordinal)))
                    {
                        try { _ = new System.Text.RegularExpressions.Regex(line.Substring(3)); }
                        catch (ArgumentException) { Check(false, lesson.Id + ": некорректное регулярное выражение " + line); }
                    }
                    Check(CourseCatalog.Find(lesson.Id) == lesson, lesson.Id + ": Find находит урок");
                }
            }
            Check(CourseCatalog.Find("нет-такого") == null, "Find неизвестного id — null");
            Check(CourseCatalog.AllLessons.Any(l => l.Theory.Contains("~layout", StringComparison.Ordinal)), "в курсе есть интроспекция");
        }

        private static async Task TheoryExamples()
        {
            int examples = 0, withOutput = 0;
            foreach (Lesson lesson in CourseCatalog.AllLessons)
            {
                foreach ((string code, IReadOnlyList<string>? expected) in Snippets.Examples(LessonMarkup.Parse(lesson.Theory)))
                {
                    examples++;
                    Snippets.Outcome outcome = await Snippets.RunAsync(code);
                    CheckEqual<string?>(null, outcome.Error, lesson.Id + ": пример из теории выполняется без ошибок");
                    if (expected != null)
                    {
                        withOutput++;
                        CheckEqual(string.Join("\n", expected), string.Join("\n", outcome.Output), lesson.Id + ": вывод примера совпадает с показанным в теории");
                    }
                }
            }
            Check(examples >= 25, "примеров в теории: " + examples);
            Check(withOutput >= 24, "из них со сверкой вывода: " + withOutput);
        }

        private static async Task SolutionsPass()
        {
            foreach (Lesson lesson in CourseCatalog.AllLessons)
            {
                CheckResult result = await LessonChecker.CheckAsync(lesson, lesson.Solution);
                Check(result.Passed, lesson.Id + ": решение проходит проверку — " + result.Status + ": " + result.Message);
            }
        }

        private static async Task StartersFail()
        {
            foreach (Lesson lesson in CourseCatalog.AllLessons)
            {
                try
                {
                    using DebugSession probe = DebugSession.Compile(lesson.Starter);
                }
                catch (GlassCompileException ex)
                {
                    Check(false, lesson.Id + ": заготовка не компилируется — " + ex.Message);
                    continue;
                }
                CheckResult result = await LessonChecker.CheckAsync(lesson, lesson.Starter);
                Check(!result.Passed, lesson.Id + ": заготовка не должна проходить проверку сама по себе");
                Check(result.Status != CheckStatus.CompileError, lesson.Id + ": заготовка запускается (" + result.Status + ")");
            }
        }

        private static async Task CheckerFailures()
        {
            Lesson hello = CourseCatalog.Find("hello")!;
            CheckResult compile = await LessonChecker.CheckAsync(hello, "fn main() {\n    print(\"x\"\n}\n");
            CheckEqual(CheckStatus.CompileError, compile.Status, "ошибка компиляции");
            CheckContains(compile.Message, "Строка", "в сообщении есть номер строки");
            Check(compile.Diagnostic != null && compile.Diagnostic.Line > 0, "диагностика с позицией");

            CheckResult runtime = await LessonChecker.CheckAsync(hello, "fn main() {\n    let xs = [1]\n    print(xs[5])\n}\n");
            CheckEqual(CheckStatus.RuntimeError, runtime.Status, "рантайм-ошибка");
            CheckContains(runtime.Message, "вне границ", "понятная причина");

            CheckResult wrong = await LessonChecker.CheckAsync(hello, "fn main() {\n    print(\"Я учу C\")\n}\n");
            CheckEqual(CheckStatus.WrongOutput, wrong.Status, "неверный вывод");
            CheckContains(wrong.Message, "Строка 1 вывода", "указана строка расхождения");
            CheckContains(wrong.Message, "Я учу Glass", "показано, что ожидалось");

            CheckResult none = await LessonChecker.CheckAsync(hello, "fn main() {\n}\n");
            CheckContains(none.Message, "ничего не напечатала", "пустой вывод");

            CheckResult extra = await LessonChecker.CheckAsync(hello, "fn main() {\n    print(\"Я учу Glass\")\n    print(\"лишнее\")\n}\n");
            CheckContains(extra.Message, "Лишний вывод", "лишние строки");

            CheckResult shortOutput = await LessonChecker.CheckAsync(CourseCatalog.Find("arithmetic")!, "fn main() {\n    print(60)\n}\n");
            CheckContains(shortOutput.Message, "Выведено строк: 1, а ожидалось: 2", "не хватает строк");

            Lesson forLesson = CourseCatalog.Find("for")!;
            CheckResult missing = await LessonChecker.CheckAsync(forLesson, "fn main() {\n    print(1)\n    print(4)\n    print(9)\n    print(16)\n    print(25)\n}\n");
            CheckEqual(CheckStatus.MissingFragment, missing.Status, "верный вывод, но без обязательного приёма");
            CheckContains(missing.Message, "for", "названо, что добавить");

            CheckResult trailing = await LessonChecker.CheckAsync(hello, "fn main() {\n    print(\"Я учу Glass   \")\n}\n");
            Check(trailing.Passed, "пробелы в конце строки не мешают");

            var started = DateTime.UtcNow;
            CheckResult timeout = await LessonChecker.CheckAsync(hello, "fn main() {\n    while true {\n    }\n}\n", TimeSpan.FromMilliseconds(300));
            CheckEqual(CheckStatus.Timeout, timeout.Status, "бесконечный цикл ловится по таймауту");
            CheckContains(timeout.Message, "бесконечный цикл", "подсказка про бесконечный цикл");
            Check((DateTime.UtcNow - started).TotalSeconds < 5, "проверка не зависла");

            // Шаблоны «re:» и сравнение
            CheckEqual<string?>(null, LessonChecker.CompareOutput(new[] { "re:^a\\d+$" }, new[] { "a123" }), "регулярное выражение совпало");
            CheckContains(LessonChecker.CompareOutput(new[] { "re:^a\\d+$" }, new[] { "b1" }) ?? "", "по шаблону", "несовпадение шаблона описано");
        }

        // ------------------------------------------------------------------ прогресс

        private static void ProgressStore()
        {
            string path = TempFile();
            try
            {
                var store = new CourseProgressStore(path);
                CourseProgress empty = store.Load();
                CheckEqual(0, empty.CompletedCount, "нет файла — чистый прогресс");

                empty.Record("hello").Completed = true;
                empty.Record("hello").Stars = 3;
                empty.Record("variables").Completed = true;
                empty.Record("variables").Stars = 2;
                empty.Record("if").Attempts = 4;   // начат, но не пройден
                empty.LastLessonId = "if";
                Check(store.Save(empty), "сохранение");

                CourseProgress loaded = new CourseProgressStore(path).Load();
                CheckEqual(2, loaded.CompletedCount, "пройдено 2");
                CheckEqual(5, loaded.TotalStars, "звёзды только за пройденные");
                CheckEqual(4, loaded.Record("if").Attempts, "попытки непройденного урока сохранены");
                CheckEqual("if", loaded.LastLessonId, "последний открытый урок");
                Check(loaded.IsCompleted("hello") && !loaded.IsCompleted("if"), "признак «пройден»");
                Check(!File.Exists(path + ".tmp"), "временный файл убран");

                // Повреждённый файл: курс не падает, испорченное откладывается.
                File.WriteAllText(path, "{ это не json");
                CourseProgress recovered = new CourseProgressStore(path).Load();
                CheckEqual(0, recovered.CompletedCount, "повреждённый файл — чистый прогресс");
                Check(File.Exists(path + ".bad"), "испорченный файл отложен рядом");

                // Нельзя записать — не падаем.
                string blocker = TempFile();
                File.WriteAllText(blocker, "файл вместо каталога");
                Check(!new CourseProgressStore(Path.Combine(blocker, "p.json")).Save(new CourseProgress()), "запись невозможна — false, без исключения");
                File.Delete(blocker);
            }
            finally
            {
                foreach (string f in new[] { path, path + ".bad", path + ".tmp" }) if (File.Exists(f)) File.Delete(f);
            }
        }

        // ------------------------------------------------------------------ список уроков и кнопка «Начать»

        private static void ListAndStartButton()
        {
            string path = TempFile();
            try
            {
                var host = new FakeCourseHost();
                var vm = new CourseViewModel(new CourseProgressStore(path), host);
                CheckEqual("Начать курс", vm.StartText, "пока ничего не пройдено");

                // Строка урока в списке выбирает его собственной командой — так WPF-список работает без кода в окне.
                LessonItem third = vm.AllLessons[2];
                Check(third.SelectCommand.CanExecute(null), "команда строки доступна");
                third.SelectCommand.Execute(null);
                Check(ReferenceEquals(vm.SelectedLesson, third), "команда выбрала свой урок");
                Check(third.IsSelected && !vm.AllLessons[0].IsSelected, "выбран ровно один урок");

                // Пройден один урок — кнопка зовёт продолжить.
                var some = new CourseProgress();
                LessonRecord first = some.Record(vm.AllLessons[0].Lesson.Id);
                first.Completed = true;
                first.Stars = 3;
                new CourseProgressStore(path).Save(some);
                var partial = new CourseViewModel(new CourseProgressStore(path), host);
                CheckEqual("Продолжить курс", partial.StartText, "часть уроков пройдена");

                // Пройдено всё — «Повторить с начала», а старт открывает первый урок.
                var every = new CourseProgress();
                foreach (Lesson lesson in CourseCatalog.Chapters.SelectMany(c => c.Lessons))
                {
                    LessonRecord record = every.Record(lesson.Id);
                    record.Completed = true;
                    record.Stars = 3;
                }
                new CourseProgressStore(path).Save(every);
                var done = new CourseViewModel(new CourseProgressStore(path), host);
                Check(done.IsAllDone, "все уроки пройдены");
                CheckEqual("Повторить с начала", done.StartText, "курс пройден");
                done.StartCommand.Execute(null);
                CheckEqual("1.1", done.SelectedLesson!.NumberText, "повтор начинается с первого урока");
            }
            finally
            {
                foreach (string f in new[] { path, path + ".bad", path + ".tmp" }) if (File.Exists(f)) File.Delete(f);
            }
        }

        // ------------------------------------------------------------------ окно курса

        private static async Task ViewModelFlow()
        {
            string path = TempFile();
            try
            {
                var host = new FakeCourseHost();
                var vm = new CourseViewModel(new CourseProgressStore(path), host);
                Check(vm.ShowWelcome && !vm.HasLesson, "сначала — приветственный экран");
                CheckEqual("Пройдено 0 из 24", vm.ProgressText, "прогресс");
                CheckEqual("Уровень: Новичок", vm.LevelText, "уровень");
                CheckEqual(6, vm.Chapters.Count, "главы");
                CheckEqual("Глава 1. Первые шаги", vm.Chapters[0].HeaderText, "заголовок главы");
                CheckEqual("0 из 5", vm.Chapters[0].ProgressText, "прогресс главы");

                vm.StartCommand.Execute(null);
                Check(vm.HasLesson && !vm.ShowWelcome, "после «Начать» открыт урок");
                CheckEqual("1.1  Привет, Glass!", vm.LessonTitle, "первый урок");
                CheckEqual("Глава 1 · Первые шаги", vm.LessonSubtitle, "подзаголовок");
                Check(vm.Theory.Count > 3 && vm.Theory.OfType<CodeBlock>().Any(), "теория разобрана на блоки");
                Check(vm.TaskText.PlainText.Contains("Я учу Glass", StringComparison.Ordinal), "текст задачи");
                Check(vm.SelectedLesson!.IsSelected, "урок помечен выбранным");
                Check(!vm.CanGoPrevious && vm.CanGoNext, "навигация на первом уроке");

                // Загрузка заготовки.
                vm.LoadStarterCommand.Execute(null);
                CheckEqual(vm.SelectedLesson.Lesson.Starter, host.Source, "заготовка попала в редактор");
                CheckContains(host.LoadedTitle ?? "", "1.1", "заголовок окна с номером урока");

                // Неверное решение (заготовка) → неудача, но попытка записана.
                await vm.CheckAsync();
                CheckEqual(ResultKind.Failure, vm.Result, "заготовка не проходит");
                Check(vm.ShowOutputComparison || vm.ResultMessage.Length > 0, "показано, чего не хватает");
                CheckEqual("Пока не совпадает", vm.ResultTitle, "заголовок неудачи");
                Check(!vm.IsChecking, "проверка закончилась");

                // Верное решение.
                host.Source = vm.SelectedLesson.Lesson.Solution;
                await vm.CheckAsync();
                CheckEqual(ResultKind.Success, vm.Result, "решение проходит");
                CheckContains(vm.ResultTitle, "★★★", "две попытки без подсказки — три звезды");
                CheckEqual("Пройдено 1 из 24", vm.ProgressText, "прогресс вырос");
                CheckEqual("★★★", vm.SelectedLesson.StarsText, "звёзды у урока в списке");
                Check(vm.SelectedLesson.IsCompleted, "урок помечен пройденным");
                CheckEqual("1 из 5", vm.Chapters[0].ProgressText, "прогресс главы");
                CheckContains(vm.ResultMessage, "+30", "очки");
                Check(Math.Abs(vm.ProgressPercent - 100.0 / 24) < 0.001, "процент прогресса");

                // Прогресс пережил перезапуск; «Начать» ведёт к первому непройденному.
                var again = new CourseViewModel(new CourseProgressStore(path), host);
                CheckEqual("Пройдено 1 из 24", again.ProgressText, "прогресс сохранён на диске");
                again.StartCommand.Execute(null);
                CheckEqual("1.2", again.SelectedLesson!.NumberText, "следующий непройденный урок");
            }
            finally
            {
                foreach (string f in new[] { path, path + ".bad", path + ".tmp" }) if (File.Exists(f)) File.Delete(f);
            }
        }

        private static async Task ViewModelDetails()
        {
            string path = TempFile();
            try
            {
                var host = new FakeCourseHost();
                var log = new AppLog(Path.Combine(Path.GetTempPath(), "glass-course-log-" + Guid.NewGuid().ToString("N")), memoryCapacity: 100);
                var vm = new CourseViewModel(new CourseProgressStore(path, log), host, log, TimeSpan.FromMilliseconds(500));

                // Подсказка → 2 звезды.
                vm.SelectedLesson = vm.AllLessons[1];
                Check(!vm.IsHintVisible && vm.HintCommand.CanExecute(null), "подсказка ещё закрыта");
                vm.HintCommand.Execute(null);
                Check(vm.IsHintVisible && !vm.HintCommand.CanExecute(null), "подсказка открыта, кнопка гаснет");
                CheckContains(vm.HintText, "let", "текст подсказки");
                host.Source = vm.SelectedLesson.Lesson.Solution;
                await vm.CheckAsync();
                CheckContains(vm.ResultTitle, "★★☆", "с подсказкой — две звезды");

                // Решение → 1 звезда; загрузка решения в редактор.
                vm.SelectedLesson = vm.AllLessons[2];
                Check(!vm.LoadSolutionCommand.CanExecute(null), "пока решение закрыто, вставить его нельзя");
                vm.SolutionCommand.Execute(null);
                Check(vm.IsSolutionVisible && vm.LoadSolutionCommand.CanExecute(null), "решение открыто");
                vm.LoadSolutionCommand.Execute(null);
                CheckEqual(vm.SelectedLesson.Lesson.Solution, host.Source, "решение вставлено в редактор");
                CheckContains(host.LoadedTitle ?? "", "решение", "в заголовке отмечено, что это решение");
                await vm.CheckAsync();
                CheckContains(vm.ResultTitle, "★☆☆", "после просмотра решения — одна звезда");
                CheckEqual("★☆☆", vm.SelectedLesson.StarsText, "звёзды в списке");
                CheckEqual("Пройдено 2 из 24", vm.ProgressText, "два пройдено");

                // Много попыток → 2 звезды; повторная проверка не отнимает звёзды.
                vm.SelectedLesson = vm.AllLessons[0];
                host.Source = "fn main() {\n}\n";
                await vm.CheckAsync();
                await vm.CheckAsync();
                host.Source = vm.SelectedLesson.Lesson.Solution;
                await vm.CheckAsync();
                CheckContains(vm.ResultTitle, "★★☆", "на третьей попытке — две звезды");
                host.Source = "fn main() {\n}\n";
                await vm.CheckAsync();
                host.Source = vm.SelectedLesson.Lesson.Solution;
                await vm.CheckAsync();
                CheckEqual("★★☆", vm.SelectedLesson.StarsText, "звёзды не уменьшаются от повторных проверок");
                CheckContains(vm.ResultTitle, "Верно!", "повторное прохождение — «Верно», а не «Урок пройден»");

                // Хозяин редактора отказал: в редакторе несохранённые изменения.
                host.Accept = false;
                vm.LoadStarterCommand.Execute(null);
                CheckContains(vm.StatusText, "остались ваши изменения", "ученик предупреждён");
                host.Accept = true;

                // Навигация.
                vm.SelectedLesson = vm.AllLessons[0];
                Check(!vm.CanGoPrevious && vm.PreviousCommand.CanExecute(null) == false, "на первом уроке назад нельзя");
                vm.NextCommand.Execute(null);
                CheckEqual("1.2", vm.SelectedLesson!.NumberText, "вперёд");
                vm.PreviousCommand.Execute(null);
                CheckEqual("1.1", vm.SelectedLesson!.NumberText, "назад");
                vm.SelectedLesson = vm.AllLessons[^1];
                Check(!vm.CanGoNext, "на последнем уроке вперёд нельзя");
                vm.SelectLessonCommand.Execute(vm.AllLessons[4]);
                CheckEqual("1.5", vm.SelectedLesson!.NumberText, "выбор урока из списка");
                vm.ShowWelcomeCommand.Execute(null);
                Check(vm.ShowWelcome, "возврат на приветственный экран");

                // Бесконечный цикл в решении: проверка не зависает.
                vm.SelectedLesson = vm.AllLessons[0];
                host.Source = "fn main() {\n    while true {\n    }\n}\n";
                await vm.CheckAsync();
                CheckEqual("Слишком долго", vm.ResultTitle, "таймаут проверки");

                // Сброс.
                Check(vm.CompletedCount >= 3, "до сброса есть прогресс");
                vm.ResetProgressCommand.Execute(null);
                CheckEqual(0, vm.CompletedCount, "после сброса прогресса нет");
                Check(vm.ShowWelcome, "после сброса — приветственный экран");
                CheckEqual(0, new CourseProgressStore(path).Load().CompletedCount, "и на диске тоже");

                Check(log.Snapshot().Any(e => e.Message.Contains("пройден", StringComparison.Ordinal)), "прохождение попало в журнал");
                var dir = new DirectoryInfo(log.Directory);
                if (dir.Exists) dir.Delete(true);
            }
            finally
            {
                foreach (string f in new[] { path, path + ".bad", path + ".tmp" }) if (File.Exists(f)) File.Delete(f);
            }
        }
    }
}
