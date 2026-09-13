import QtQuick
import QtQuick.Controls as C
import org.kde.spectacle.private

Item {
    id: root
    objectName: "spectacleOcrButton"
    implicitWidth: buttons.implicitWidth
    implicitHeight: buttons.implicitHeight
    width: implicitWidth
    height: implicitHeight
    visible: !SpectacleCore.videoMode && SpectacleCore.ocrAvailable
    readonly property bool canRecognize: visible && SpectacleCore.ocrStatus !== 1
    Binding { target: socrOriginalButton; property: "visible"; value: false }
    Row {
        id: buttons
        C.ToolButton {
            objectName: "socrRecognize"
            icon.name: socrUi.formula ? "insert-math-expression" : "document-scan"
            text: SpectacleCore.ocrStatus === 1 ? (socrUi.formula ? "正在识别公式…" : "正在识别文字…")
                                               : (socrUi.formula ? "公式识别" : "文本识别")
            enabled: root.canRecognize
            onClicked: { if (!socrUi.formula || socrUi.prepare()) SpectacleCore.startOcrExtraction() }
            C.ToolTip.visible: hovered
            C.ToolTip.text: socrUi.formula ? "将截图中的单个公式转换为 LaTeX" : "提取截图中的中英文文字"
        }
        C.ToolButton {
            objectName: "socrModeMenu"
            text: "▾"
            enabled: root.canRecognize
            onClicked: modes.popup()
            C.Menu {
                id: modes
                objectName: "socrModes"
                C.MenuItem { objectName: "socrTextMode"; text: "文本识别"; enabled: root.canRecognize; checkable: true; checked: !socrUi.formula; onTriggered: socrUi.formula = false }
                C.MenuItem { objectName: "socrFormulaMode"; text: "公式识别 → LaTeX"; enabled: root.canRecognize; checkable: true; checked: socrUi.formula; onTriggered: socrUi.formula = true }
            }
        }
    }
    C.Dialog {
        id: errorDialog
        title: "公式识别失败"
        modal: true
        anchors.centerIn: C.Overlay.overlay
        standardButtons: C.Dialog.Ok
        width: 440
        contentItem: C.Label { id: errorLabel; wrapMode: Text.Wrap }
    }
    Connections {
        target: socrUi
        function onFailed(message) { errorLabel.text = message; errorDialog.open() }
    }
}
