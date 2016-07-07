#pragma once

namespace Analytics
{
	struct Result
	{
		enum Enum
		{
			Ok = 0,
			Failed,
			CannotOpenDatabase,
			CannotCreateTables,
			DatabaseIsReadonly,
		};
	};
}