/***************************************************************************
 *   Copyright (C) 2008-2014 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <algorithm>
#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "bindings.h"
#include "configuration.h"
#include "config.h"
#include "mpdpp.h"
#include "settings.h"
#include "utility/string.h"

namespace po = boost::program_options;

using std::cerr;
using std::cout;

namespace {

const char *env_home;

std::string xdg_config_home()
{
	std::string result;
	const char *env_xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (env_xdg_config_home == nullptr)
		result = "~/.config/";
	else
	{
		result = env_xdg_config_home;
		if (!result.empty() && result.back() != '/')
			result += "/";
	}
	return result;
}

}

void expand_home(std::string &path)
{
	assert(env_home != nullptr);
	if (!path.empty() && path[0] == '~')
		path.replace(0, 1, env_home);
}

bool configure(int argc, char **argv)
{
	const std::vector<std::string> default_config_paths = {
		"~/.ncmpcpp/config",
		xdg_config_home() + "ncmpcpp/config"
	};

	std::string bindings_path;
	std::vector<std::string> config_paths;

	po::options_description options("Options");
	options.add_options()
		("host,h", po::value<std::string>()->default_value("localhost"), "connect to server at host")
		("port,p", po::value<int>()->default_value(6600), "connect to server at port")
		("config,c", po::value<std::vector<std::string>>(&config_paths)->default_value(default_config_paths, join<std::string>(default_config_paths, " AND ")), "specify configuration file(s)")
		("ignore-config-errors", po::value<bool>()->default_value(false), "ignore unknown and invalid options in configuration files")
		("bindings,b", po::value<std::string>(&bindings_path)->default_value("~/.ncmpcpp/bindings"), "specify bindings file")
		("screen,s", po::value<std::string>(), "specify initial screen")
		("slave-screen,S", po::value<std::string>(), "specify initial slave screen")
		("help,?", "show help message")
		("version,v", "display version information")
	;

	po::variables_map vm;
	try
	{
		po::store(po::parse_command_line(argc, argv, options), vm);

		if (vm.count("help"))
		{
			cout << "Usage: " << argv[0] << " [options]...\n" << options << "\n";
			return false;
		}
		if (vm.count("version"))
		{
			std::cout << "ncmpcpp " << VERSION << "\n\n"
			<< "optional screens compiled-in:\n"
	#		ifdef HAVE_TAGLIB_H
			<< " - tag editor\n"
			<< " - tiny tag editor\n"
	#		endif
	#		ifdef HAVE_CURL_CURL_H
			<< " - artist info\n"
	#		endif
	#		ifdef ENABLE_OUTPUTS
			<< " - outputs\n"
	#		endif
	#		ifdef ENABLE_VISUALIZER
			<< " - visualizer\n"
	#		endif
	#		ifdef ENABLE_CLOCK
			<< " - clock\n"
	#		endif
			<< "\nencoding detection: "
	#		ifdef HAVE_LANGINFO_H
			<< "enabled"
	#		else
			<< "disabled"
	#		endif // HAVE_LANGINFO_H
			<< "\nbuilt with support for:"
	#		ifdef HAVE_CURL_CURL_H
			<< " curl"
	#		endif
	#		ifdef HAVE_FFTW3_H
			<< " fftw"
	#		endif
			<< " ncurses"
	#		ifdef HAVE_TAGLIB_H
			<< " taglib"
	#		endif
	#		ifdef NCMPCPP_UNICODE
			<< " unicode"
	#		endif
			<< "\n";
			return false;
		}

		po::notify(vm);
		// get home directory
		env_home = getenv("HOME");
		if (env_home == nullptr)
		{
			cerr << "Fatal error: HOME environment variable is not defined\n";
			return false;
		}

		// read configuration
		std::for_each(config_paths.begin(), config_paths.end(), expand_home);
		if (Config.read(config_paths, vm["ignore-config-errors"].as<bool>()) == false)
			exit(1);

		// if bindings file was not specified, use the one from main directory.
		if (vm["bindings"].defaulted())
			bindings_path = Config.ncmpcpp_directory + "bindings";
		else
			expand_home(bindings_path);

		// read bindings
		if (Bindings.read(bindings_path) == false)
			exit(1);
		Bindings.generateDefaults();

		// create directories
		boost::filesystem::create_directory(Config.ncmpcpp_directory);
		boost::filesystem::create_directory(Config.lyrics_directory);

		// try to get MPD connection details from environment variables
		// as they take precedence over these from the configuration.
		auto env_host = getenv("MPD_HOST");
		auto env_port = getenv("MPD_PORT");
		if (env_host != nullptr)
			Mpd.SetHostname(env_host);
		if (env_port != nullptr)
			Mpd.SetPort(boost::lexical_cast<int>(env_port));

		// if MPD connection details are provided as command line
		// parameters, use them as their priority is the highest.
		if (!vm["host"].defaulted())
			Mpd.SetHostname(vm["host"].as<std::string>());
		if (!vm["port"].defaulted())
			Mpd.SetPort(vm["port"].as<int>());
		Mpd.SetTimeout(Config.mpd_connection_timeout);

		// custom startup screen
		if (vm.count("screen"))
		{
			auto screen = vm["screen"].as<std::string>();
			Config.startup_screen_type = stringtoStartupScreenType(screen);
			if (Config.startup_screen_type == ScreenType::Unknown)
			{
				std::cerr << "Unknown screen: " << screen << "\n";
				exit(1);
			}
		}

		// custom startup slave screen
		if (vm.count("slave-screen"))
		{
			auto screen = vm["slave-screen"].as<std::string>();
			Config.startup_slave_screen_type = stringtoStartupScreenType(screen);
			if (Config.startup_slave_screen_type == ScreenType::Unknown)
			{
				std::cerr << "Unknown slave screen: " << screen << "\n";
				exit(1);
			}
		}
	}
	catch (std::exception &e)
	{
		cerr << "Error while processing configuration: " << e.what() << "\n";
		exit(1);
	}
	return true;
}
