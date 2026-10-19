// Строки списков и таблиц IDE: неизменяемые «карточки», которые XAML показывает
// как есть. Вся логика форматирования — здесь, а не в конвертерах WPF: так её
// проверяют тесты.

using System;
using System.Globalization;
using System.Windows.Input;
using Glass.Studio.Examples;
using Glass.Studio.Models;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    /// <summary>Пункт меню «Примеры»: у каждого своя команда, чтобы XAML не нуждался в привязках «вверх по дереву».</summary>
    public sealed class ExampleMenuItem
    {
        public ExampleMenuItem(ExampleItem example, Action<ExampleItem> open, Func<bool> canOpen)
        {
            Example = example;
            OpenCommand = new RelayCommand(() => open(example), canOpen);
        }

        public ExampleItem Example { get; }
        public string Title => Example.Title;
        public string Description => Example.Description;
        public RelayCommand OpenCommand { get; }
        public ICommand Command => OpenCommand;
    }

    public sealed class StackFrameItem
    {
        public StackFrameItem(int depth, string function, int line)
        {
            Depth = depth;
            Function = function;
            Line = line;
        }

        public int Depth { get; }
        public string Function { get; }
        public int Line { get; }
        public string LineText => "строка " + Line.ToString(CultureInfo.InvariantCulture);
        public bool IsCurrent => Depth == 0;
    }

    public sealed class LocalItem
    {
        public LocalItem(LocalVariable variable)
        {
            Name = variable.Name;
            Value = variable.Value;
        }

        public string Name { get; }
        public GlassValue Value { get; }
        public bool IsReference => Value.IsReference;
        public string? Address => Value.Address;

        /// <summary>Что показывать в колонке «Тип»: примитив, имя mold'а или array[N].</summary>
        public string TypeText
        {
            get
            {
                switch (Value.Kind)
                {
                    case ValueKind.Int: return "int";
                    case ValueKind.Double: return "double";
                    case ValueKind.Bool: return "bool";
                    case ValueKind.String: return "string";
                    case ValueKind.Object: return Value.ClassName ?? "object";
                    case ValueKind.Array: return "array[" + Value.Length.ToString(CultureInfo.InvariantCulture) + "]";
                    default: return "null";
                }
            }
        }

        /// <summary>Значение: примитив как есть, ссылка — «→ @…f3b0» (по адресу объект находится на схеме кучи).</summary>
        public string ValueText => IsReference ? "→ @" + AddressFormat.Short(Value.Address!) : Value.Display;
    }

    public sealed class ProblemItem
    {
        public ProblemItem(Diagnostic diagnostic)
        {
            Diagnostic = diagnostic;
        }

        public Diagnostic Diagnostic { get; }
        public DiagnosticKind Kind => Diagnostic.Kind;
        public int Line => Diagnostic.Line;
        public string Message => Diagnostic.Message;
        public string KindText => Kind == DiagnosticKind.Compile ? "Компиляция" : "Выполнение";

        public string LocationText
        {
            get
            {
                if (Diagnostic.Line <= 0) return "";
                string text = "строка " + Diagnostic.Line.ToString(CultureInfo.InvariantCulture);
                if (Diagnostic.Column > 0) text += ", столбец " + Diagnostic.Column.ToString(CultureInfo.InvariantCulture);
                return text;
            }
        }
    }
}
