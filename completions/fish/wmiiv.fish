# wmiiv(1) completion

complete -f -c wmiiv
complete -c wmiiv -s h -l help --description "Show help message and quit."
complete -c wmiiv -s c -l config --description "Specifies a config file." -r
complete -c wmiiv -s C -l validate --description "Check the validity of the config file, then exit."
complete -c wmiiv -s d -l debug --description "Enables full logging, including debug information."
complete -c wmiiv -s v -l version --description "Show the version number and quit."
complete -c wmiiv -s V -l verbose --description "Enables more verbose logging."
complete -c wmiiv -l get-socketpath --description "Gets the IPC socket path and prints it, then exits."

