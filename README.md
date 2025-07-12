# SLSHELL



This is a very basic shell mostly Posix compliant still working on that part
Working on allowing customs prompts, you can change the prompt right now at 

in the "" you can change it

snprintf(prompt, sizeof(prompt), " \033[1;34m[%s]\033[0m \033[1;35m(%s)\033[0m $ ", user, hostname, cwd);


Install
add /usr/bin/SLSHELL to /etc/shells
sudo chsh -s /usr/bin/SLSHELL "user"
logout out and log back in
let me know how the shell works or issues and I will get around to fixing it
