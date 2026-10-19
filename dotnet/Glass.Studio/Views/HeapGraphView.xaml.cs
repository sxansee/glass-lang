using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Glass.Studio.Heap;
using Glass.Studio.ViewModels;

namespace Glass.Studio.Views
{
    /// <summary>Схема кучи. Клики превращаются в выбор объекта; всё остальное — в XAML и Core.</summary>
    public partial class HeapGraphView : UserControl
    {
        public HeapGraphView()
        {
            InitializeComponent();
        }

        private MainViewModel? ViewModel => DataContext as MainViewModel;

        private void OnNodeMouseDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement element && element.DataContext is HeapNodeModel node)
            {
                ViewModel?.SelectHeapNode(node.Address);
                e.Handled = true;   // не доходим до фона, который снимает выбор
            }
        }

        private void OnRootMouseDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement element && element.DataContext is RootModel root)
            {
                ViewModel?.SelectHeapNode(root.TargetAddress);
                e.Handled = true;
            }
        }

        private void OnBackgroundMouseDown(object sender, MouseButtonEventArgs e)
        {
            ViewModel?.SelectHeapNode(null);
        }
    }
}
