// Инспектор объекта: «как он устроен в памяти». Раскладка полей и vtable берутся
// у VM теми же функциями, что считают ~layout и ~vtable в самом языке, — то,
// что здесь написано, не может разойтись с тем, что напечатает программа.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Glass.Studio.Models;
using Glass.Studio.Mvvm;

namespace Glass.Studio.ViewModels
{
    public sealed class FieldRow
    {
        public FieldRow(int offset, string name, string type, int size, string valueText, bool isReference, string? targetAddress)
        {
            Offset = offset;
            Name = name;
            Type = type;
            Size = size;
            ValueText = valueText;
            IsReference = isReference;
            TargetAddress = targetAddress;
        }

        public int Offset { get; }
        public string Name { get; }
        public string Type { get; }
        public int Size { get; }
        /// <summary>Подпись под именем в «карте слотов»: «слот 1 · 8 Б».</summary>
        public string SlotText => "слот " + Offset.ToString(CultureInfo.InvariantCulture) + " · " + Size.ToString(CultureInfo.InvariantCulture) + " Б";
        public string ValueText { get; }
        public bool IsReference { get; }
        public string? TargetAddress { get; }
    }

    public sealed class VTableRow
    {
        public VTableRow(string method, string implementedBy, bool isInherited)
        {
            Method = method;
            ImplementedBy = implementedBy;
            IsInherited = isInherited;
        }

        public string Method { get; }
        public string ImplementedBy { get; }
        /// <summary>true — реализация унаследована от предка; false — класс объекта переопределил/объявил метод сам.</summary>
        public bool IsInherited { get; }
        public string Target => ImplementedBy + "." + Method;
    }

    public sealed class ElementRow
    {
        public ElementRow(string index, string valueText, bool isReference, string? targetAddress)
        {
            Index = index;
            ValueText = valueText;
            IsReference = isReference;
            TargetAddress = targetAddress;
        }

        public string Index { get; }
        public string ValueText { get; }
        public bool IsReference { get; }
        public string? TargetAddress { get; }
    }

    public sealed class ObjectInspectorViewModel : ObservableObject
    {
        private static readonly IReadOnlyList<FieldRow> NoFields = Array.Empty<FieldRow>();
        private static readonly IReadOnlyList<VTableRow> NoVTable = Array.Empty<VTableRow>();
        private static readonly IReadOnlyList<ElementRow> NoElements = Array.Empty<ElementRow>();

        private bool _hasSelection;
        private bool _isArray;
        private bool _isGarbage;
        private string _title = "";
        private string _addressText = "";
        private string _ancestryText = "";
        private string _sizeSummary = "";
        private string _note = "";
        private IReadOnlyList<FieldRow> _fields = NoFields;
        private IReadOnlyList<VTableRow> _vtable = NoVTable;
        private IReadOnlyList<ElementRow> _elements = NoElements;

        public bool HasSelection { get => _hasSelection; private set => SetProperty(ref _hasSelection, value); }
        public bool IsArray { get => _isArray; private set => SetProperty(ref _isArray, value); }
        public bool IsObject => HasSelection && !IsArray;
        public bool IsGarbage { get => _isGarbage; private set => SetProperty(ref _isGarbage, value); }

        /// <summary>«Dog» или «array[3]».</summary>
        public string Title { get => _title; private set => SetProperty(ref _title, value); }

        /// <summary>«0x7f3a9c2e10b0  (дес. 139…)» — то же число, что вернёт ~address.</summary>
        public string AddressText { get => _addressText; private set => SetProperty(ref _addressText, value); }

        /// <summary>«Dog → Animal → Shape»: сам класс, предки, facet'ы.</summary>
        public string AncestryText { get => _ancestryText; private set => SetProperty(ref _ancestryText, value); }

        public string SizeSummary { get => _sizeSummary; private set => SetProperty(ref _sizeSummary, value); }

        /// <summary>Пояснение под заголовком (например, «недостижим — следующая сборка его освободит»).</summary>
        public string Note { get => _note; private set => SetProperty(ref _note, value); }
        public bool HasNote => !string.IsNullOrEmpty(Note);

        public IReadOnlyList<FieldRow> Fields { get => _fields; private set => SetProperty(ref _fields, value); }
        public IReadOnlyList<VTableRow> VTable { get => _vtable; private set => SetProperty(ref _vtable, value); }
        public IReadOnlyList<ElementRow> Elements { get => _elements; private set => SetProperty(ref _elements, value); }

        public void Clear()
        {
            HasSelection = false;
            IsArray = false;
            IsGarbage = false;
            Title = "";
            AddressText = "";
            AncestryText = "";
            SizeSummary = "";
            SetNote("");
            Fields = NoFields;
            VTable = NoVTable;
            Elements = NoElements;
            OnPropertyChanged(nameof(IsObject));
        }

        /// <param name="obj">Объект из снимка кучи (значения полей).</param>
        /// <param name="info">Устройство от VM (раскладка, vtable); null — объект уже собран.</param>
        public void Show(HeapObject obj, ObjectInfo? info)
        {
            string decimalAddress = obj.Address;
            AddressText = AddressFormat.Hex(decimalAddress) + "  (~address = " + decimalAddress + ")";
            IsGarbage = !obj.Reachable;
            SetNote(obj.Reachable ? "" : "Недостижим: ни одна переменная и ни один живой объект на него не ссылаются. Следующая сборка мусора его освободит.");

            if (obj.IsArray)
            {
                IsArray = true;
                Title = "array[" + obj.Members.Count.ToString(CultureInfo.InvariantCulture) + "]";
                AncestryText = "";
                SizeSummary = "";
                Fields = NoFields;
                VTable = NoVTable;
                Elements = obj.Members.Select(m => new ElementRow(
                    m.Name, m.Value.IsReference ? "→ " + m.Value.Describe() : m.Value.Display,
                    m.Value.IsReference, m.Value.Address)).ToList();
            }
            else
            {
                IsArray = false;
                Title = obj.ClassName;
                Elements = NoElements;
                AncestryText = info == null ? "" : string.Join(" → ", info.Ancestry);

                var fields = new List<FieldRow>();
                int totalBytes = 0;
                if (info != null)
                {
                    foreach (FieldLayout slot in info.Layout)
                    {
                        LocalVariable? member = obj.Members.FirstOrDefault(m => m.Name == slot.Name);
                        GlassValue? value = member?.Value;
                        fields.Add(new FieldRow(slot.Offset, slot.Name, slot.Type, slot.Size,
                            value == null ? "?" : (value.IsReference ? "→ " + value.Describe() : value.Display),
                            value != null && value.IsReference, value?.Address));
                        totalBytes += slot.Size;
                    }
                    SizeSummary = fields.Count == 0
                        ? "Полей нет"
                        : fields.Count.ToString(CultureInfo.InvariantCulture) + " " + Plural.Choose(fields.Count, "поле", "поля", "полей") +
                          " · " + totalBytes.ToString(CultureInfo.InvariantCulture) + " байт (условно)";
                    VTable = info.VTable.Select(e => new VTableRow(e.Method, e.ImplementedBy, e.ImplementedBy != obj.ClassName)).ToList();
                }
                else
                {
                    SizeSummary = "";
                    VTable = NoVTable;
                }
                Fields = fields;
            }

            HasSelection = true;
            OnPropertyChanged(nameof(IsObject));
        }

        private void SetNote(string text)
        {
            Note = text;
            OnPropertyChanged(nameof(HasNote));
        }
    }

    /// <summary>Русские окончания: 1 объект, 2 объекта, 5 объектов, 21 объект.</summary>
    public static class Plural
    {
        public static string Choose(int n, string one, string few, string many)
        {
            int abs = Math.Abs(n);
            int lastTwo = abs % 100;
            int last = abs % 10;
            if (lastTwo >= 11 && lastTwo <= 14) return many;
            if (last == 1) return one;
            if (last >= 2 && last <= 4) return few;
            return many;
        }
    }
}
