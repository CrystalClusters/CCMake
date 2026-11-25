func main() -> void {
    if (host_info("os") == "windows") {
        command("cmd.exe", {"/c", "exit", "3"});
    } else {
        command("sh", {"-c", "exit 3"});
    }
}
