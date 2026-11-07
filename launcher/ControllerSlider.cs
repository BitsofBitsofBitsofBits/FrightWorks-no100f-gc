using Avalonia.Controls;

namespace ScoobyReMasked;

sealed class ControllerSlider : Slider
{
    public ControllerSlider(double controllerStep) => ControllerStep = controllerStep;
    public double ControllerStep { get; }
    protected override Type StyleKeyOverride => typeof(Slider);
}
