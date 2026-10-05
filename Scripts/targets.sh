# Module graph shared by build.sh and test.sh. Keep in sync with Package.swift.
LIBS="WhiteprintCore WhiteprintExtract WhiteprintRender WhiteprintBridge WhiteprintStudy WhiteprintEditor"
EXES="WhiteprintApp whiteprint-mcp"

deps() {
    case $1 in
        WhiteprintCore | WhiteprintExtract) echo "" ;;
        WhiteprintRender | WhiteprintBridge) echo "WhiteprintCore" ;;
        WhiteprintStudy) echo "WhiteprintCore WhiteprintExtract" ;;
        WhiteprintEditor) echo "WhiteprintCore WhiteprintRender" ;;
        WhiteprintApp) echo "WhiteprintCore WhiteprintExtract WhiteprintRender WhiteprintBridge WhiteprintStudy WhiteprintEditor" ;;
        whiteprint-mcp) echo "WhiteprintCore WhiteprintBridge" ;;
        *) echo "unknown target: $1" >&2; exit 1 ;;
    esac
}
