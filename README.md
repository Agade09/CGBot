# CGBot
Codingame.com chat bot based on Markov Chains. Made to work as a daemon which takes command line inputs, continually listens to cin for chat messages and sometimes outputs on cout.

Command line inputs:
	* Chat_Room_Name in the format: `general_fr@conference.codingame.com`
	* Nickname of the bot, e.g: `Neumam`
	* A list of ignored talkers separated by spaces, e.g: `Automaton2000 AgadeBot Neumann`. These speakers will be ignored for the purposes of learning speech and triggering responses

Continually listens for input on std::in and expects:
	* 1 line of chat log in the format: `MadKnight : how is your CSB?`

Outputs on std::out if the chatbot's name is found in a non-ignored speaker's message:
	* 1 line of randomly generated text

Inspired from https://github.com/dreignier/cgbot.