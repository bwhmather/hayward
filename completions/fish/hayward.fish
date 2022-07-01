# hayward(1) completion

complete -f -c hayward
complete -c hayward -s h -l help --description "Show help message and quit."
complete -c hayward -s c -l config --description "Specifies a config file." -r
complete -c hayward -s C -l validate --description "Check the validity of the config file, then exit."
complete -c hayward -s d -l debug --description "Enables full logging, including debug information."
complete -c hayward -s v -l version --description "Show the version number and quit."
complete -c hayward -s V -l verbose --description "Enables more verbose logging."
complete -c hayward -l get-socketpath --description "Gets the IPC socket path and prints it, then exits."

