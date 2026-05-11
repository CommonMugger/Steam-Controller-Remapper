#define NOMINMAX
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <string>

using namespace winrt;
using namespace Windows::Gaming::Input;

static constexpr uint16_t VALVE_VID = 0x28DE;
static constexpr uint16_t SC_PID    = 0x1302;

int main() {
    init_apartment();

    printf("=== RawGameController Probe ===\n");
    printf("Close Steam before running. Enable Game Mode in the tray app first.\n\n");
    printf("Enumerating (waiting 1s for OS enumeration)...\n");
    Sleep(1000);

    auto controllers = RawGameController::RawGameControllers();

    if (controllers.Size() == 0) {
        printf("No RawGameController devices found.\n");
        return 0;
    }

    printf("All detected controllers:\n");
    RawGameController target = nullptr;
    for (auto const& ctrl : controllers) {
        auto name = to_string(ctrl.DisplayName());
        printf("  \"%s\"  VID=%04X PID=%04X  Buttons=%u Axes=%u Switches=%u\n",
            name.c_str(),
            ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
            ctrl.ButtonCount(), ctrl.AxisCount(), ctrl.SwitchCount());

        if (ctrl.HardwareVendorId() == VALVE_VID && ctrl.HardwareProductId() == SC_PID)
            target = ctrl;
    }

    printf("\n");

    if (!target) {
        printf("Steam Controller (VID=28DE PID=1302) not found.\n");
        printf("The OS may not be exposing the vendor HID collection to RawGameController.\n");
        printf("Registry mapping approach will NOT work.\n");
        return 1;
    }

    uint32_t btnCount    = target.ButtonCount();
    uint32_t axisCount   = target.AxisCount();
    uint32_t switchCount = target.SwitchCount();

    printf("Steam Controller found!\n");
    printf("  Buttons : %u\n", btnCount);
    printf("  Axes    : %u\n", axisCount);
    printf("  Switches: %u\n\n", switchCount);

    if (btnCount == 0 && axisCount == 0 && switchCount == 0) {
        printf("Device has no enumerable inputs.\n");
        printf("Registry mapping approach will NOT work.\n");
        return 1;
    }

    printf("Registry mapping approach looks viable — polling live values.\n");
    printf("Move sticks, press buttons. Press Ctrl+C to exit.\n\n");

    // vector<bool> is a bitset without .data(); use a heap bool array instead.
    auto buttons    = std::make_unique<bool[]>(btnCount ? btnCount : 1);
    auto prevButtons= std::make_unique<bool[]>(btnCount ? btnCount : 1);
    std::vector<double>  axes(axisCount, 0.0);
    std::vector<GameControllerSwitchPosition> switches(switchCount ? switchCount : 1);
    std::vector<double>  prevAxes(axisCount, -1.0);
    for (uint32_t i = 0; i < btnCount; i++) { buttons[i] = false; prevButtons[i] = false; }

    while (true) {
        // API order: buttons, switches, axes
        target.GetCurrentReading(
            winrt::array_view<bool>(buttons.get(), buttons.get() + btnCount),
            winrt::array_view<GameControllerSwitchPosition>(switches.data(), switches.data() + switchCount),
            winrt::array_view<double>(axes.data(), axes.data() + axisCount));

        for (uint32_t i = 0; i < axisCount; i++) {
            if (std::abs(axes[i] - prevAxes[i]) > 0.001) {
                printf("  Axis[%02u] = %.4f\n", i, axes[i]);
                prevAxes[i] = axes[i];
            }
        }

        for (uint32_t i = 0; i < btnCount; i++) {
            if (buttons[i] != prevButtons[i]) {
                printf("  Button[%02u] = %s\n", i, buttons[i] ? "PRESSED" : "released");
                prevButtons[i] = buttons[i];
            }
        }

        Sleep(16);
    }
}
