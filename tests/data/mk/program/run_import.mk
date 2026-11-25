@import func prepare from "run_lib.mk";

func main() -> void {
    int value = prepare();
    info("value: %d", value);
}
