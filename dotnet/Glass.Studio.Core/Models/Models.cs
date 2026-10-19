// Модели данных IDE — то, что приходит из glass_capi (JSON) после разбора.
// Намеренно простые неизменяемые типы без зависимостей от WPF: на них
// строятся и граф кучи, и таблицы локальных переменных, и тесты.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;

namespace Glass.Studio.Models
{
    public enum ValueKind { Null, Int, Double, Bool, String, Object, Array }

    /// <summary>
    /// Значение Glass, как его видит отладчик: примитив целиком, а объект/массив —
    /// только ссылкой по адресу (сами данные — в снимке кучи, см. GlassApi.h).
    /// </summary>
    public sealed class GlassValue
    {
        public static readonly GlassValue Null = new GlassValue(ValueKind.Null, "null");

        private const int MaxStringDisplay = 28;

        private GlassValue(ValueKind kind, string display, string? address = null, string? className = null, int length = 0)
        {
            Kind = kind;
            Display = display;
            Address = address;
            ClassName = className;
            Length = length;
        }

        public ValueKind Kind { get; }

        /// <summary>Короткий текст для ячейки: 42, 3.14, true, "abc", Dog, [3].</summary>
        public string Display { get; }

        /// <summary>Для Object/Array — адрес в куче (десятичная строка, как в JSON).</summary>
        public string? Address { get; }

        /// <summary>Для Object — имя mold'а.</summary>
        public string? ClassName { get; }

        /// <summary>Для Array — число элементов.</summary>
        public int Length { get; }

        public bool IsReference => Kind == ValueKind.Object || Kind == ValueKind.Array;

        public static GlassValue FromInt(long value) =>
            new GlassValue(ValueKind.Int, value.ToString(CultureInfo.InvariantCulture));

        /// <summary>text — как напечатал бы print() (в том числе "inf"/"nan").</summary>
        public static GlassValue FromDouble(string text) => new GlassValue(ValueKind.Double, text);

        public static GlassValue FromBool(bool value) => new GlassValue(ValueKind.Bool, value ? "true" : "false");

        public static GlassValue FromString(string value)
        {
            string flat = value.Replace("\\", "\\\\").Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t");
            if (flat.Length > MaxStringDisplay) flat = flat.Substring(0, MaxStringDisplay) + "…";
            return new GlassValue(ValueKind.String, "\"" + flat + "\"");
        }

        public static GlassValue ObjectRef(string address, string className) =>
            new GlassValue(ValueKind.Object, className, address, className);

        public static GlassValue ArrayRef(string address, int length) =>
            new GlassValue(ValueKind.Array, "[" + length.ToString(CultureInfo.InvariantCulture) + "]", address, null, length);

        /// <summary>Для таблиц: ссылки — с коротким адресом ("Dog @…f3b0"), остальное как Display.</summary>
        public string Describe() => IsReference ? Display + " @" + AddressFormat.Short(Address!) : Display;

        public override string ToString() => Describe();
    }

    /// <summary>Адреса приходят десятичными строками (64-битно безопасно); людям удобнее hex.</summary>
    public static class AddressFormat
    {
        public static string Hex(string decimalAddress)
        {
            return BigInteger.TryParse(decimalAddress, NumberStyles.None, CultureInfo.InvariantCulture, out BigInteger value)
                ? "0x" + value.ToString("x", CultureInfo.InvariantCulture).TrimStart('0').PadLeft(1, '0')
                : decimalAddress;
        }

        /// <summary>Последние 4 hex-цифры с многоточием — достаточно, чтобы различать объекты на экране.</summary>
        public static string Short(string decimalAddress)
        {
            string hex = Hex(decimalAddress);
            if (hex.StartsWith("0x", StringComparison.Ordinal)) hex = hex.Substring(2);
            return "…" + (hex.Length > 4 ? hex.Substring(hex.Length - 4) : hex);
        }
    }

    public sealed record StackFrameInfo(string Function, int Line);

    public sealed record LocalVariable(string Name, GlassValue Value);

    /// <summary>Локальные переменные одного фрейма вызова (Depth 0 — текущий, самый глубокий).</summary>
    public sealed record FrameLocals(int Depth, string Function, int Line, IReadOnlyList<LocalVariable> Locals);

    /// <summary>Объект или массив кучи. Members — поля объекта либо элементы массива ("[0]", "[1]", ...).</summary>
    public sealed record HeapObject(
        string Address, bool IsArray, string ClassName, bool Reachable, IReadOnlyList<LocalVariable> Members);

    public sealed record HeapSnapshot(IReadOnlyList<HeapObject> Objects)
    {
        public static readonly HeapSnapshot Empty = new HeapSnapshot(Array.Empty<HeapObject>());
    }

    public sealed record FieldLayout(string Name, int Offset, int Size, string Type);

    public sealed record VTableEntry(string Method, string ImplementedBy);

    /// <summary>Устройство одного объекта: раскладка полей и vtable — то же, что ~layout и ~vtable.</summary>
    public sealed record ObjectInfo(
        string Address,
        bool IsArray,
        string ClassName,
        int Length,
        IReadOnlyList<string> Ancestry,
        IReadOnlyList<FieldLayout> Layout,
        IReadOnlyList<VTableEntry> VTable);

    public enum DiagnosticKind { Compile, Runtime }

    /// <summary>Ошибка с позицией — для подчёркивания строки в редакторе.</summary>
    public sealed record Diagnostic(DiagnosticKind Kind, int Line, int Column, string Message);
}
