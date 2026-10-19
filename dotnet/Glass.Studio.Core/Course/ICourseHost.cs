namespace Glass.Studio.Course
{
    /// <summary>Что курсу нужно от IDE: прочитать код ученика и подставить заготовку урока.</summary>
    public interface ICourseHost
    {
        /// <summary>Текущий текст редактора.</summary>
        string GetSource();

        /// <summary>Заменяет текст редактора. false — пользователь отказался терять несохранённые изменения.</summary>
        bool LoadProgram(string text, string title);
    }
}
