/**
 * Linux Shell in C++.
 * @author Jaime Sendín
 * @date Jan 11 2022
 * @file shell.cc
 * @brief Creation of a shell for users to interact with the operating system. 
 */

#include <iostream>
#include <cstring>
#include <vector>
#include <array>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error> 
#include <assert.h>
#include <libgen.h>
#include <utime.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <functional>
#include <unistd.h>
#include <sstream>
#include <cassert>
#include <errno.h>


namespace shell 
{
using command = std::vector<std::string>;
}

namespace shell 
{
	struct command_result 
	{
		int return_value;
		bool is_quit_requested;
	
		command_result(int return_value, bool request_quit=false) : return_value{return_value}, is_quit_requested{request_quit} {}
		
		static command_result quit(int return_value=0)
		{
			return command_result{return_value, true};
		}
	};
}

shell::command_result result(0, false);

std::error_code print(const std::string& str) 
{
	int bytes_written = write(STDOUT_FILENO, str.c_str(), str.size());

	if (bytes_written == -1) 
	{
    std::error_code error{errno, std::system_category()};
    std::cerr << "Error when typing prompt" << '\n';
    return error;
	}

  std::error_code success{0, std::system_category()};
  return success;
}

void print_prompt(int last_command_status) // imprimir el prompt de la shell
{
  char machine_pointer[100];
  size_t length = 100;
  getlogin_r(machine_pointer, length);
  std::string user = machine_pointer;
  gethostname(machine_pointer, length);
  std::string machine = machine_pointer;
  std::string cwd = getcwd(machine_pointer, length);
  
	std::string symbol;
	
	if(last_command_status == 0)
	{
		symbol= "$> ";
	} 
	else 
	{
		symbol = "$< ";
	}
	
	std::string command_line = user + "@" + machine + ":" + cwd + " " + symbol;
	
  if(isatty(STDIN_FILENO)){
  	print(command_line);
  }
}

int read_line(int fd, std::string& line)
{
	static std::vector<uint8_t> pending_input;
	pending_input.clear();

	while(true)
	{
		for(int i{0}; i < pending_input.size(); ++i)
		{
			if(pending_input[i] == '\n')
			{
				for(int j{0}; j <= i; ++j)
				{
					line.push_back(pending_input[j]);
				}
				pending_input.erase(pending_input.begin(),pending_input.begin()+i);
				return 0;
			}
		}
	
		std::array<uint8_t, 256> buffer; 
		ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
		if(bytes_read < 0)
		{
			return errno;
		} 
		else if(bytes_read != 0)
		{
			if(buffer.empty())
			{
				if(!pending_input.empty())
				{
					for(int i{0}; i < pending_input.size(); ++i)
					{
						line.push_back(pending_input[i]);
					}
					line.push_back('\n');
					pending_input.clear();
				}
				return 0;
			} 
			else 
			{
				for(int i{0}; i < bytes_read; ++i)
				{
					pending_input.push_back(buffer[i]);
				}
			}
		} 
		else 
		{
			return 0;
		}
	}
}

std::vector<shell::command> parse_line(const std::string& line)
{
	std::vector<shell::command> commands;
	shell::command arguments;
	
	std::istringstream iss(line);

	while(!iss.eof())
	{
		std::string word;
		iss >> word;

		if(word.size()==0) break;

		char last_character = word[word.size() - 1];
		
		if(last_character == ';' || last_character == '&' || last_character == '|')
		{
			if(word.length() > 1)
			{
				word.pop_back();
				arguments.push_back(word);
			}
			arguments.push_back(std::string(1, last_character));
			commands.push_back(arguments);
			
			arguments.clear();
		}
		else if (last_character == '#')
		{
			commands.push_back(arguments);
			return commands;
		} 
		else 
		{
			if(word[0] == '#')
			{
				commands.push_back(arguments);
				return commands;
			} 
			else 
			{
				arguments.push_back(word);
			}
		}
	}

	if(!arguments.empty())
	{
		commands.push_back(arguments);
		arguments.clear();
	}
	
	return commands;
}

int echo_command(const std::vector<std::string>& args) // printing a text on the screen
{
	std::string output;
	for(int i{1}; i < args.size(); ++i)
	{
		output = output + args[i] + " ";
	}
	output.pop_back();
	output.push_back('\n');

	std::cout << output;

	return 0;
}

int cd_command(const std::vector<std::string>& args) // change directory
{
	if(args.size() != 2)
	{
		std::cerr << "ERROR: Too many arguments" << '\n';
		return 1;
	}

	return chdir(args[1].c_str());
}

ssize_t write(int fd, const std::vector<uint8_t>& buffer, ssize_t bytes_read)
{
	ssize_t bytes_write = write(fd, buffer.data(), bytes_read);
	return bytes_write;
}

ssize_t read(int fd, std::vector<uint8_t>& buffer) 
{
	ssize_t bytes_read = read(fd, buffer.data(), buffer.size());

  if (bytes_read < 0) 
	{
  	return errno;
  }

  return bytes_read;
}

int check(const char *path, bool dir)
{
	struct stat path_stat;
  stat(path, &path_stat);
	
	if(dir == true)
	{
		return S_ISDIR(path_stat.st_mode);
	} 
	else 
	{
		return S_ISREG(path_stat.st_mode);
	}
}

void copy_file(const std::string& src_path, const std::string& dst_path, bool preserve_all=false)
{
	int real_uid = getuid(); // uid = user ID
	int effective_uid = geteuid();
	setuid(real_uid);
	
	std::string dest_route;
	
	if (access(src_path.c_str(), F_OK) != 0) 
	{
		throw std::system_error(1, std::system_category(), std::string(__FILE__) + " #" + std::to_string(__LINE__));
	}
	
	if(check(src_path.c_str(), false) == 0)
	{
		throw std::system_error(2, std::system_category(), std::string(__FILE__) + " #" + std::to_string(__LINE__)); 
	}
	
	if(access(dst_path.c_str(), F_OK) != -1)
	{  
		assert(strcmp(src_path.c_str(), dst_path.c_str()) != 0);
		if(check(dst_path.c_str(), true) == 1)
		{ 
			char* filename = basename((char*) src_path.c_str());
			dest_route = dst_path + "/" + filename;
		}
	}
	
	std::vector<uint8_t> buffer(1024);

	int origin = open(src_path.c_str(), O_RDONLY); 
	int destiny = open(dest_route.c_str(), O_CREAT | O_WRONLY | O_SYNC); 
	
	ssize_t bytes_read;

	while((bytes_read = read(origin, buffer)) > 0)
	{
		write(destiny, buffer, bytes_read); 
	}
	
	close(origin); 
	close(destiny); 

	if (preserve_all == true)
	{ 
		struct stat st;
        
		stat(src_path.c_str(), &st);
		lchown(dest_route.c_str(), st.st_uid, st.st_gid);

		stat(src_path.c_str(), &st);
    chmod(dest_route.c_str(), st.st_mode);

		struct utimbuf src_time 
		{
    st.st_atime, st.st_mtime
    };

    utime(dest_route.c_str(), &src_time);
	}

	setuid(effective_uid);
}

void move_file(const std::string& src_path, const std::string& dst_path)
{
	int real_uid = getuid();
	setuid(real_uid);
	int effective_uid = geteuid();
	
	std::string dest_route;
	
	if (access(src_path.c_str(), F_OK) != 0) 
	{ 
		throw std::system_error(1, std::system_category(), std::string(__FILE__) + " #" + std::to_string(__LINE__));
	}
	
	if(check(src_path.c_str(), false) == 0)
	{
		throw std::system_error(2, std::system_category(), std::string(__FILE__) + " #" + std::to_string(__LINE__)); 
	}
	
	if(access(dst_path.c_str(), F_OK) != -1)
	{  
		assert(strcmp(src_path.c_str(), dst_path.c_str()) != 0); 

		if(check(dst_path.c_str(), true) == 1)
		{ 
			char* filename = basename((char*) src_path.c_str());
			dest_route = dst_path + "/" + filename; 
		}
	}
	
	std::vector<uint8_t> buffer(1024); 

	int origin = open(src_path.c_str(), O_RDONLY);
	int destiny = open(dest_route.c_str(), O_CREAT | O_WRONLY | O_SYNC); 
	
	ssize_t bytes_read;

	while((bytes_read = read(origin, buffer)) > 0)
	{
		write(destiny, buffer, bytes_read); 
	}

	close(origin); 
	close(destiny); 

  struct stat st; 

	stat(src_path.c_str(), &st);
	lchown(dest_route.c_str(), st.st_uid, st.st_gid); 

  stat(src_path.c_str(), &st);
  chmod(dest_route.c_str(), st.st_mode); 

	struct utimbuf src_time 
	{
  st.st_atime, st.st_mtime
  };

  utime(dest_route.c_str(), &src_time); 

	setuid(effective_uid);

  remove(src_path.c_str());
}

int cp_command(const std::vector<std::string>& args)
{
	if(args.size() == 3)
	{
		copy_file(args[1], args[2], false);
	} 
	else if (args.size() == 4)
	{
		if(args[1] == "-a")
		{
			copy_file(args[2], args[3], true);
		} 
		else 
		{
			std::cerr << "Syntax is incorrect" << '\n';
			return 1;
		}
	} 
	else 
	{
		std::cerr << "Syntax is incorrect" << '\n'; 
		return 1;
	}

	return 0;
}

int mv_command(const std::vector<std::string>& args)
{
	if(args.size() == 3)
	{
		move_file(args[1], args[2]);
	} 
	else 
	{
		std::cerr << "Syntax is incorrect" << '\n';
		return 1;
	}

	return 0;
}

int execute(const std::vector<std::string> &args) 
{
	std::vector<const char*> argv;

	for (const auto& arg : args)
	{
		argv.push_back(arg.c_str());
	}
	
	argv[argv.size()] = nullptr;

	return execvp(argv[0], const_cast<char* const*>(argv.data()));
}

int execute_program(const std::vector<std::string> &args, bool has_wait = true)
{
	pid_t child = fork();
	int status;

	if (child == 0) 
	{
		status = execute(args);
		exit(status);
	}
	else if (child > 0) 
	{
	  if(has_wait) // The parent process waits for the child process to terminate and returns the output value of the child process
		{
			wait(&status);
			status = WEXITSTATUS(status);
		} 
		else // We return immediately with the PID of the child process
		{
			return child;
		}		
	} 
	else 
	{
		std::cerr << "Error when trying to create child process";
		return EXIT_FAILURE;
	}

	return status;
}

int spawn_proccess(std::function<int (const std::vector<std::string>&)>& command, const std::vector<std::string>& args, bool has_wait=true)
{
  // In order not to have a version of execute_program() for each internal command, we are going to create a new version
  // where the function invoked in the created child process is configurable
	
  pid_t child = fork();
  int status;

  if (child == 0) 
	{
    try
		{
			status = command(args);
		} 
	  catch (std::system_error &e)
		{
			std::cerr << e.what() << '\n';
			status = e.code().value();
		}
    exit(status);
  }
  else if (child > 0) 
	{
		if(has_wait) // The parent process waits for the child process to terminate and returns the output value of the child process
		{
			wait(&status);
    	status = WEXITSTATUS(status);
		} 
		else // We return immediately with the PID of the child process
		{
    	return child;
    }    
  } 
	else 
	{
		std::cerr << "Error when trying to create child process";
    return EXIT_FAILURE;
  }

  return status;
}

shell::command_result execute_commands(const std::vector<shell::command>& commands)
{
	std::vector<shell::command> command = commands;

	static std::vector<pid_t> pending_pid; // pid = process id 

	bool foreground{true};
	int res{0};

	for(pid_t child_pid : pending_pid)
	{
		if(child_pid != 0)
		{
			int status;
      int res = waitpid(child_pid, &status, WNOHANG);
      if(res!=0)
			{
				for(int i{0}; i < pending_pid.size(); ++i)
				{
					if(pending_pid[i] == child_pid)
					{
						pending_pid.erase(pending_pid.begin() + i);
					}
				}
				std::cout << "Value returned by the process with pid " << child_pid << ": " << status << '\n';
      }
    }
	}

	for(auto& cmd : command)
	{
		if(cmd[0] == "exit")
		{
			return shell::command_result::quit(res);
		}
		if(cmd.back() == "&" || cmd.back() == ";" || cmd.back() == "|")
		{
			if(cmd.back() == "&")
			{
				foreground = false;
			}
			cmd.pop_back();
		}
		if(foreground == true)
		{
			if(cmd[0] == "echo") 
			{
				res = echo_command(cmd);
			} 
			else if(cmd[0] == "cd")
			{
				res = cd_command(cmd);
			} 
			else if(cmd[0] == "cp")
			{
				try
				{
					res = cp_command(cmd);
				} 
				catch(std::system_error& e)
				{
					std::cerr << e.what() << '\n';
					res = e.code().value();
				}
			} 
			else if (cmd[0] == "mv")
			{
				try
				{
					res = mv_command(cmd);
				} 
				catch(std::system_error& e)
				{
					std::cerr << e.what() << '\n';
					res = e.code().value();
				}
			} 
			else 
			{
				res = execute_program(cmd, foreground);
			}
		} 
		else 
		{
			std::cout << "Background" << '\n';
			if(cmd[0] == "echo")
			{
				std::function<int (const std::vector<std::string>&)> echo_function = echo_command;
				res = spawn_proccess(echo_function,cmd, false);
			} 
			else if(cmd[0] == "cd")
			{
				std::function<int (const std::vector<std::string>&)> cd_function = cd_command;
				res = spawn_proccess(cd_function,cmd, false);
			} 
			else if(cmd[0] == "cp")
			{
				std::function<int (const std::vector<std::string>&)> cp_function = cp_command;
				res = spawn_proccess(cp_function,cmd, false);
			} 
			else if (cmd[0] == "mv")
			{
				std::function<int (const std::vector<std::string>&)> mv_function = mv_command;
				res = spawn_proccess(mv_function,cmd, false);
			} 
			else 
			{
				res = execute_program(cmd, foreground);
			}
		}
		if(!foreground)
		{
			pending_pid.push_back(res);
		}   
	}

	return shell::command_result(res);
}

int main()
{
	std::string order;
	
	while(true) // main loop
	{
    if(read_line == 0 && order.empty()) 
    {
      return result.return_value;
    } 
    else
    {
      print_prompt(result.return_value);
      read_line(STDIN_FILENO, order);
      std::vector<shell::command> commands = parse_line(order);
      //if(!commands.empty())
      //{
        auto [return_value, is_quit_requested] = execute_commands(commands);
        if (is_quit_requested) 
        {
          return return_value;
        }
      //}
      order.clear();
    }
	}
	return 0;
}