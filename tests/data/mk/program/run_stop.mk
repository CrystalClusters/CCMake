func main() -> void {
    remove("out/run_missing");
    copy("tests/data/mk/program/config.mc", "out/run_after_fail");
}
