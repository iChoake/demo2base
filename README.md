# demo2base

Convert bases from City of Heroes .cohdemo format to dbquery containers.

## usage

Enter the Supergroup Base you wish to export. Run the command `/demo_record MyBase` followed by `/demo_stop` in order to create a file called `client_demos/MyBase.cohdemo` inside your game folder.

Run `demo2base.exe MyBase.cohdemo MyBase.txt` in order to expand the base to its text form suitable for import with dbquery. Copy it to your server folder.

Find the `ContainerId` of a supergroup by running the command `bin\dbquery.exe -dbquery -find 6 Name "Supergroup Name"`.

Import the base to that supergroup with the command `bin\dbquery.exe -dbquery -setbase ContainerId MyBase.txt`
