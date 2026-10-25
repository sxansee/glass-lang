using System;
using System.Windows;
using Glass.Studio.ViewModels;

namespace Glass.Studio
{
    /// <summary>
    /// Окно курса. Только показывает CourseViewModel: вся логика (выбор урока, подсказки, звёзды, прогресс, проверка решения
    /// настоящей VM) живёт в ядре и проверяется тестами. Окно не модальное — рядом с редактором, где ученик пишет решение.
    /// </summary>
    public partial class CourseWindow : Window
    {
        private readonly CourseViewModel _viewModel;

        public CourseWindow(CourseViewModel viewModel)
        {
            InitializeComponent();
            _viewModel = viewModel;
            DataContext = viewModel;
        }

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            // Справа у верхнего края рабочей области: слева остаётся редактор, и урок виден рядом с кодом.
            Rect area = SystemParameters.WorkArea;
            Left = Math.Max(area.Left, area.Right - Width - 16);
            Top = area.Top + 70;
        }

        private void OnResetProgress(object sender, RoutedEventArgs e)
        {
            MessageBoxResult answer = MessageBox.Show(this,
                "Сбросить прогресс курса? Пройденные уроки и звёзды будут потеряны.",
                "Курс по Glass", MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No);
            if (answer == MessageBoxResult.Yes) _viewModel.ResetProgressCommand.Execute(null);
        }
    }
}
