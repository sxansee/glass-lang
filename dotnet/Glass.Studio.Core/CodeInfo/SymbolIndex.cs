// Индекс объявлений в исходнике: функции, mold/facet с полями и методами, переменные, параметры.
// Строится по лексемам SourceScanner без полного разбора, поэтому работает на недописанном коде. Это эвристика
// для подсказок: настоящую проверку типов делает компилятор; здесь — «что объявлено выше и какого типа».

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Glass.Studio.CodeInfo
{
    public sealed record FieldSymbol(string Name, string Type, int Line);
    public sealed record ParameterSymbol(string Name, string Type);

    public sealed record FunctionSymbol(string Name, string? Owner, string Signature, string ReturnType,
        IReadOnlyList<ParameterSymbol> Parameters, int Line, int BodyStart, int BodyEnd);

    public sealed record MoldSymbol(string Name, bool IsFacet, string? Base, IReadOnlyList<string> Facets, int Line,
        IReadOnlyList<FieldSymbol> Fields, IReadOnlyList<FunctionSymbol> Methods, FunctionSymbol? Constructor);

    public enum VariableKind { Local, Parameter, LoopVariable, CatchVariable }

    public sealed record VariableSymbol(string Name, string Type, bool IsInferred, int Line, int TokenIndex, VariableKind Kind, FunctionSymbol? Scope);

    public sealed class SymbolIndex
    {
        private readonly string _source;
        private readonly IReadOnlyList<SourceToken> _tokens;
        private readonly List<FunctionSymbol> _functions = new List<FunctionSymbol>();
        private readonly List<MoldSymbol> _molds = new List<MoldSymbol>();
        private readonly List<VariableSymbol> _variables = new List<VariableSymbol>();
        private readonly List<FunctionSymbol> _allFunctions = new List<FunctionSymbol>();

        public SymbolIndex(string source, IReadOnlyList<SourceToken> tokens)
        {
            _source = source;
            _tokens = tokens;
            Build();
        }

        public IReadOnlyList<FunctionSymbol> Functions => _functions;
        public IReadOnlyList<MoldSymbol> Molds => _molds;
        public IReadOnlyList<VariableSymbol> Variables => _variables;

        public FunctionSymbol? FindFunction(string name) => _functions.FirstOrDefault(f => f.Name == name);
        public MoldSymbol? FindMold(string name) => _molds.FirstOrDefault(m => m.Name == name);

        /// <summary>Функция/метод, тело которой охватывает лексему с этим номером (самая внутренняя).</summary>
        public FunctionSymbol? EnclosingFunction(int tokenIndex) =>
            _allFunctions.Where(f => f.BodyStart <= tokenIndex && tokenIndex <= f.BodyEnd).OrderByDescending(f => f.BodyStart).FirstOrDefault();

        /// <summary>Ближайшее выше по тексту объявление имени в той же функции.</summary>
        public VariableSymbol? FindVariable(string name, int tokenIndex)
        {
            FunctionSymbol? scope = EnclosingFunction(tokenIndex);
            return _variables.Where(v => v.Name == name && v.Scope == scope && v.TokenIndex <= tokenIndex)
                             .OrderByDescending(v => v.TokenIndex).FirstOrDefault();
        }

        /// <summary>Поле или метод mold (с учётом цепочки предков).</summary>
        public (MoldSymbol Owner, FieldSymbol? Field, FunctionSymbol? Method)? FindMember(string moldName, string memberName)
        {
            var visited = new HashSet<string>();
            for (MoldSymbol? mold = FindMold(moldName); mold != null && visited.Add(mold.Name); mold = mold.Base == null ? null : FindMold(mold.Base))
            {
                FieldSymbol? field = mold.Fields.FirstOrDefault(f => f.Name == memberName);
                if (field != null) return (mold, field, null);
                FunctionSymbol? method = mold.Methods.FirstOrDefault(m => m.Name == memberName);
                if (method != null) return (mold, null, method);
            }
            return null;
        }

        // ------------------------------------------------------------------ построение

        private string T(int index) => _tokens[index].Text(_source);
        private bool IsOp(int index, string text) => index >= 0 && index < _tokens.Count && _tokens[index].Kind == TokenKind.Operator && T(index) == text;
        private bool IsKw(int index, string text) => index >= 0 && index < _tokens.Count && _tokens[index].Kind == TokenKind.Keyword && T(index) == text;
        private bool IsIdent(int index) => index >= 0 && index < _tokens.Count && _tokens[index].Kind == TokenKind.Identifier;

        /// <summary>Индекс закрывающей скобки для открывающей в позиции open; если не закрыта — последняя лексема.</summary>
        private int MatchBrace(int open, string openText, string closeText)
        {
            int depth = 0;
            for (int i = open; i < _tokens.Count; i++)
            {
                if (IsOp(i, openText)) depth++;
                else if (IsOp(i, closeText) && --depth == 0) return i;
            }
            return _tokens.Count - 1;
        }

        private string JoinTokens(int from, int toExclusive)
        {
            var text = new StringBuilder();
            for (int i = from; i < toExclusive && i < _tokens.Count; i++) text.Append(T(i));
            return text.ToString();
        }

        private void Build()
        {
            var moldStarts = new List<(int Index, bool IsFacet)>();
            for (int i = 0; i < _tokens.Count; i++)
            {
                if (IsKw(i, "mold") || IsKw(i, "facet"))
                {
                    int bodyEnd = ParseMold(i);
                    if (bodyEnd > i) i = bodyEnd;
                }
                else if (IsKw(i, "fn"))
                {
                    FunctionSymbol? fn = ParseFunction(i, null);
                    if (fn != null)
                    {
                        _functions.Add(fn);
                        _allFunctions.Add(fn);
                        i = Math.Max(i, fn.BodyEnd);
                    }
                }
            }

            // После разбора molds: что из «предков» на самом деле facet.
            for (int m = 0; m < _molds.Count; m++)
            {
                MoldSymbol mold = _molds[m];
                if (mold.Base != null && FindMold(mold.Base) is { IsFacet: true })
                {
                    _molds[m] = mold with { Base = null, Facets = new[] { mold.Base }.Concat(mold.Facets).ToList() };
                }
            }

            foreach (FunctionSymbol fn in _allFunctions) IndexVariables(fn);
        }

        private int ParseMold(int kwIndex)
        {
            bool isFacet = IsKw(kwIndex, "facet");
            int nameIndex = kwIndex + 1;
            if (!IsIdent(nameIndex)) return kwIndex;
            string name = T(nameIndex);

            string? baseName = null;
            var facets = new List<string>();
            int i = nameIndex + 1;
            if (IsOp(i, ":"))
            {
                i++;
                while (IsIdent(i))
                {
                    if (baseName == null) baseName = T(i);
                    else facets.Add(T(i));
                    i++;
                    if (IsOp(i, ",")) i++;
                }
            }
            while (i < _tokens.Count && !IsOp(i, "{")) i++;
            if (i >= _tokens.Count) return kwIndex;
            int open = i;
            int close = MatchBrace(open, "{", "}");

            var fields = new List<FieldSymbol>();
            var methods = new List<FunctionSymbol>();
            FunctionSymbol? ctor = null;
            int depth = 0;
            for (int k = open; k <= close; k++)
            {
                if (IsOp(k, "{")) { depth++; continue; }
                if (IsOp(k, "}")) { depth--; continue; }
                if (depth != 1) continue;

                if (IsKw(k, "let") && IsIdent(k + 1))
                {
                    int end = k + 2;
                    string type = "";
                    if (IsOp(end, ":"))
                    {
                        int typeEnd = end + 1;
                        while (typeEnd < close && !IsOp(typeEnd, "=") && !IsOp(typeEnd, ";") && _tokens[typeEnd].Line == _tokens[k].Line) typeEnd++;
                        type = JoinTokens(end + 1, typeEnd);
                    }
                    fields.Add(new FieldSymbol(T(k + 1), type, _tokens[k].Line));
                }
                else if (IsKw(k, "fn"))
                {
                    FunctionSymbol? method = ParseFunction(k, name);
                    if (method != null)
                    {
                        methods.Add(method);
                        _allFunctions.Add(method);
                        k = Math.Max(k, method.BodyEnd);
                        if (method.BodyEnd > open) { /* пропущено тело */ }
                    }
                }
                else if (IsIdent(k) && T(k) == name && IsOp(k + 1, "(") && !isFacet)
                {
                    FunctionSymbol? constructor = ParseFunction(k, name, isConstructor: true);
                    if (constructor != null)
                    {
                        ctor = constructor;
                        _allFunctions.Add(constructor);
                        k = Math.Max(k, constructor.BodyEnd);
                    }
                }
            }

            _molds.Add(new MoldSymbol(name, isFacet, baseName, facets, _tokens[kwIndex].Line, fields, methods, ctor));
            return close;
        }

        /// <summary>Заголовок «fn имя(параметры) [-> тип]» (или конструктор «Имя(параметры)») и границы тела.</summary>
        private FunctionSymbol? ParseFunction(int headerIndex, string? owner, bool isConstructor = false)
        {
            int nameIndex = isConstructor ? headerIndex : headerIndex + 1;
            if (!IsIdent(nameIndex) || !IsOp(nameIndex + 1, "(")) return null;
            string name = T(nameIndex);
            int open = nameIndex + 1;
            int close = MatchBrace(open, "(", ")");

            var parameters = new List<ParameterSymbol>();
            int p = open + 1;
            while (p < close)
            {
                if (IsIdent(p) && IsOp(p + 1, ":"))
                {
                    int typeEnd = p + 2;
                    int depth = 0;
                    while (typeEnd < close && !(depth == 0 && IsOp(typeEnd, ",")))
                    {
                        if (IsOp(typeEnd, "[")) depth++;
                        if (IsOp(typeEnd, "]")) depth--;
                        typeEnd++;
                    }
                    parameters.Add(new ParameterSymbol(T(p), JoinTokens(p + 2, typeEnd)));
                    p = typeEnd + 1;
                }
                else
                {
                    p++;
                }
            }

            string returnType = "void";
            int after = close + 1;
            if (IsOp(after, "->"))
            {
                int typeEnd = after + 1;
                while (typeEnd < _tokens.Count && !IsOp(typeEnd, "{") && !IsOp(typeEnd, ";") && _tokens[typeEnd].Line == _tokens[after].Line) typeEnd++;
                returnType = JoinTokens(after + 1, typeEnd);
                after = typeEnd;
            }

            int bodyStart = after, bodyEnd = after;
            if (IsOp(after, "{"))
            {
                bodyEnd = MatchBrace(after, "{", "}");
            }
            else if (IsOp(after, ";"))
            {
                bodyEnd = after;   // сигнатура без тела (facet)
            }

            // Сигнатура начинается с модификаторов («override fn speak() -> string»), если они есть.
            int first = headerIndex;
            while (first > 0 && _tokens[first - 1].Kind == TokenKind.Keyword && _tokens[first - 1].Line == _tokens[headerIndex].Line &&
                   (T(first - 1) is "virtual" or "override" or "static" or "public" or "private" or "protected")) first--;
            int headerStart = _tokens[first].Start;
            int headerEnd = bodyStart < _tokens.Count && IsOp(bodyStart, "{") ? _tokens[bodyStart].Start : (IsOp(bodyStart, ";") ? _tokens[bodyStart].Start : _tokens[Math.Min(after, _tokens.Count - 1)].End);
            string signature = string.Join(" ", _source.Substring(headerStart, Math.Max(0, headerEnd - headerStart)).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
            return new FunctionSymbol(name, owner, signature, returnType, parameters, _tokens[headerIndex].Line, bodyStart, bodyEnd);
        }

        private void IndexVariables(FunctionSymbol fn)
        {
            foreach (ParameterSymbol parameter in fn.Parameters)
            {
                _variables.Add(new VariableSymbol(parameter.Name, parameter.Type, false, fn.Line, fn.BodyStart, VariableKind.Parameter, fn));
            }

            for (int i = fn.BodyStart; i <= fn.BodyEnd && i < _tokens.Count; i++)
            {
                if (IsKw(i, "let") && IsIdent(i + 1))
                {
                    int line = _tokens[i].Line;
                    int end = i + 2;
                    string type;
                    bool inferred;
                    if (IsOp(end, ":"))
                    {
                        int typeEnd = end + 1;
                        while (typeEnd <= fn.BodyEnd && !IsOp(typeEnd, "=") && !IsOp(typeEnd, ";") && _tokens[typeEnd].Line == line) typeEnd++;
                        type = JoinTokens(end + 1, typeEnd);
                        inferred = false;
                    }
                    else
                    {
                        int rhs = IsOp(end, "=") ? end + 1 : end;
                        type = InferType(rhs, line) ?? "тип выводится из значения";
                        inferred = true;
                    }
                    _variables.Add(new VariableSymbol(T(i + 1), type, inferred, line, i + 1, VariableKind.Local, fn));
                }
                else if (IsKw(i, "for") && IsIdent(i + 1) && IsKw(i + 2, "in"))
                {
                    string? elementType = ElementTypeOfIterable(i + 3, _tokens[i].Line);
                    _variables.Add(new VariableSymbol(T(i + 1), elementType ?? "элемент коллекции", true, _tokens[i].Line, i + 1, VariableKind.LoopVariable, fn));
                }
                else if (IsKw(i, "catch") && IsIdent(i + 1) && IsOp(i + 2, ":"))
                {
                    int typeEnd = i + 3;
                    while (typeEnd <= fn.BodyEnd && !IsOp(typeEnd, "{")) typeEnd++;
                    _variables.Add(new VariableSymbol(T(i + 1), JoinTokens(i + 3, typeEnd), false, _tokens[i].Line, i + 1, VariableKind.CatchVariable, fn));
                }
            }
        }

        private string? ElementTypeOfIterable(int start, int line)
        {
            if (start >= _tokens.Count) return null;
            if (_tokens[start].Kind == TokenKind.String) return "string";
            if (IsIdent(start) && T(start) == "range" && IsOp(start + 1, "(")) return "int";
            if (_tokens[start].Kind == TokenKind.Number && IsOp(start + 1, "..")) return "int";
            if (IsOp(start, "["))
            {
                string? array = InferType(start, line);
                if (array != null && array.StartsWith("[", StringComparison.Ordinal) && array.EndsWith("]", StringComparison.Ordinal)) return array.Substring(1, array.Length - 2);
            }
            if (IsIdent(start))
            {
                VariableSymbol? v = _variables.Where(x => x.Name == T(start) && x.TokenIndex <= start).OrderByDescending(x => x.TokenIndex).FirstOrDefault();
                if (v != null && v.Type.StartsWith("[", StringComparison.Ordinal) && v.Type.EndsWith("]", StringComparison.Ordinal)) return v.Type.Substring(1, v.Type.Length - 2);
                if (v != null && v.Type == "string") return "string";
            }
            return null;
        }

        /// <summary>Тип по первым лексемам правой части (эвристика); null — не удалось определить.</summary>
        private string? InferType(int index, int line)
        {
            if (index >= _tokens.Count || _tokens[index].Line != line) return null;
            SourceToken t = _tokens[index];
            string text = T(index);

            if (t.Kind == TokenKind.Operator && text == "-" && index + 1 < _tokens.Count && _tokens[index + 1].Kind == TokenKind.Number) return InferType(index + 1, line);
            if (t.Kind == TokenKind.Number) return text.IndexOfAny(new[] { '.', 'e', 'E' }) >= 0 ? "double" : "int";
            if (t.Kind == TokenKind.String) return "string";
            if (t.Kind == TokenKind.Keyword && (text == "true" || text == "false")) return "bool";
            if (t.Kind == TokenKind.Keyword && text == "null") return "null";

            if (t.Kind == TokenKind.Operator && text == "[")
            {
                if (IsOp(index + 1, "]")) return "[?]";
                string? element = InferType(index + 1, line);
                return element == null ? "массив" : "[" + element + "]";
            }

            bool isCall = IsOp(index + 1, "(");
            if (isCall && (t.Kind == TokenKind.Identifier || t.Kind == TokenKind.Keyword))
            {
                FunctionSymbol? fn = FindFunction(text);
                if (fn != null) return fn.ReturnType;
                if (FindMold(text) != null) return text;
                return text switch
                {
                    "len" or "int" or "floor" or "ceil" or "round" => "int",
                    "str" or "string" => "string",
                    "double" or "sqrt" or "pow" => "double",
                    "bool" => "bool",
                    "range" => "[int]",
                    "abs" or "min" or "max" or "sum" => "число",
                    _ => null,
                };
            }
            if (t.Kind == TokenKind.Identifier)
            {
                VariableSymbol? v = _variables.Where(x => x.Name == text && x.TokenIndex < index).OrderByDescending(x => x.TokenIndex).FirstOrDefault();
                if (v != null && !v.Type.StartsWith("тип выводится", StringComparison.Ordinal)) return v.Type;
            }
            return null;
        }
    }
}
