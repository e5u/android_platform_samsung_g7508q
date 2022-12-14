/*
 * Copyright (C) 2012-2013 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "imv_os_state.h"

#include "imv/imv_lang_string.h"
#include "imv/imv_reason_string.h"
#include "imv/imv_remediation_string.h"

#include <tncif_policy.h>

#include <utils/debug.h>
#include <collections/linked_list.h>

typedef struct private_imv_os_state_t private_imv_os_state_t;
typedef struct package_entry_t package_entry_t;
typedef struct entry_t entry_t;
typedef struct instruction_entry_t instruction_entry_t;

/**
 * Private data of an imv_os_state_t object.
 */
struct private_imv_os_state_t {

	/**
	 * Public members of imv_os_state_t
	 */
	imv_os_state_t public;

	/**
	 * TNCCS connection ID
	 */
	TNC_ConnectionID connection_id;

	/**
	 * TNCCS connection state
	 */
	TNC_ConnectionState state;

	/**
	 * Does the TNCCS connection support long message types?
	 */
	bool has_long;

	/**
	 * Does the TNCCS connection support exclusive delivery?
	 */
	bool has_excl;

	/**
	 * Maximum PA-TNC message size for this TNCCS connection
	 */
	u_int32_t max_msg_len;

	/**
	 * Flags set for completed actions
	 */
	u_int32_t action_flags;

	/**
	 * Access Requestor ID Type
	 */
	u_int32_t ar_id_type;

	/**
	 * Access Requestor ID Value
	 */
	chunk_t ar_id_value;

	/**
	 * IMV database session associated with TNCCS connection
	 */
	imv_session_t *session;

	/**
	 * IMV action recommendation
	 */
	TNC_IMV_Action_Recommendation rec;

	/**
	 * IMV evaluation result
	 */
	TNC_IMV_Evaluation_Result eval;

	/**
	 * IMV OS handshake state
	 */
	imv_os_handshake_state_t handshake_state;

	/**
	 * OS Product Information (concatenation of OS Name and Version)
	 */
	char *info;

	/**
	 * OS Type
	 */
	os_type_t type;

	/**
	 * OS Name
	 */
	chunk_t name;

	/**
	 * OS Version
	 */
	chunk_t version;

	/**
	 * List of blacklisted packages to be removed
	 */
	linked_list_t *remove_packages;

	/**
	 * List of vulnerable packages to be updated
	 */
	linked_list_t *update_packages;

	/**
	 * TNC Reason String
	 */
	imv_reason_string_t *reason_string;

	/**
	 * IETF Remediation Instructions String
	 */
	imv_remediation_string_t *remediation_string;

	/**
	 * Dgevice ID
	 */
	chunk_t device_id;

	/**
	 * Number of processed packages
	 */
	int count;

	/**
	 * Number of not updated packages
	 */
	int count_update;

	/**
	 * Number of blacklisted packages
	 */
	int count_blacklist;

	/**
	 * Number of whitelisted packages
	 */
	int count_ok;

	/**
	 * OS Settings
	 */
	u_int os_settings;

	/**
	 * Angel count
	 */
	int angel_count;

};

/**
 * Supported languages
 */
static char* languages[] = { "en", "de", "pl" };

/**
 * Reason strings for "OS settings"
 */
static imv_lang_string_t reason_settings[] = {
	{ "en", "Improper OS settings were detected" },
	{ "de", "Unzul??ssige OS Einstellungen wurden festgestellt" },
	{ "pl", "Stwierdzono niew??a??ciwe ustawienia OS" },
	{ NULL, NULL }
};

/**
 * Reason strings for "installed software packages"
 */
static imv_lang_string_t reason_packages[] = {
	{ "en", "Vulnerable or blacklisted software packages were found" },
	{ "de", "Schwachstellenbehaftete oder gesperrte Softwarepakete wurden gefunden" },
	{ "pl", "Znaleziono pakiety podatne na atak lub b??d??ce na czarnej li??cie" },
	{ NULL, NULL }
};

/**
 * Instruction strings for "Software Security Updates"
 */
static imv_lang_string_t instr_update_packages_title[] = {
	{ "en", "Software Security Updates" },
	{ "de", "Software Sicherheitsupdates" },
	{ "pl", "Aktualizacja softwaru zabezpieczaj??cego" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_update_packages_descr[] = {
	{ "en", "Packages with security vulnerabilities were found" },
	{ "de", "Softwarepakete mit Sicherheitsschwachstellen wurden gefunden" },
	{ "pl", "Znaleziono pakiety podatne na atak" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_update_packages_header[] = {
	{ "en", "Please update the following software packages:" },
	{ "de", "Bitte updaten Sie die folgenden Softwarepakete:" },
	{ "pl", "Prosz?? zaktualizowa?? nast??puj??ce pakiety:" },
	{ NULL, NULL }
};

/**
 * Instruction strings for "Blacklisted Software Packages"
 */
static imv_lang_string_t instr_remove_packages_title[] = {
	{ "en", "Blacklisted Software Packages" },
	{ "de", "Gesperrte Softwarepakete" },
	{ "pl", "Pakiety b??d??ce na czarnej li??cie" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_remove_packages_descr[] = {
	{ "en", "Dangerous software packages were found" },
	{ "de", "Gef??hrliche Softwarepakete wurden gefunden" },
	{ "pl", "Znaleziono niebezpieczne pakiety" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_remove_packages_header[] = {
	{ "en", "Please remove the following software packages:" },
	{ "de", "Bitte entfernen Sie die folgenden Softwarepakete:" },
	{ "pl", "Prosz?? usun???? nast??puj??ce pakiety:" },
	{ NULL, NULL }
};

;/**
 * Instruction strings for "Forwarding Enabled"
 */
static imv_lang_string_t instr_fwd_enabled_title[] = {
	{ "en", "IP Packet Forwarding" },
	{ "de", "Weiterleitung von IP Paketen" },
	{ "pl", "Przekazywanie pakiet??w IP" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_fwd_enabled_descr[] = {
	{ "en", "Please disable the forwarding of IP packets" },
	{ "de", "Bitte deaktivieren Sie das Forwarding von IP Paketen" },
	{ "pl", "Prosz?? zdezaktywowa?? przekazywanie pakiet??w IP" },
	{ NULL, NULL }
};

/**
 * Instruction strings for "Default Password Enabled"
 */
static imv_lang_string_t instr_default_pwd_enabled_title[] = {
	{ "en", "Default Password" },
	{ "de", "Default Passwort" },
	{ "pl", "Has??o domy??lne" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_default_pwd_enabled_descr[] = {
	{ "en", "Please change the default password" },
	{ "de", "Bitte ??ndern Sie das Default Passwort" },
	{ "pl", "Prosz?? zmieni?? domy??lne has??o" },
	{ NULL, NULL }
};

/**
 * Instruction strings for  "Unknown Source"
 */
static imv_lang_string_t instr_unknown_source_title[] = {
	{ "en", "Unknown Software Origin" },
	{ "de", "Unbekannte Softwareherkunft" },
	{ "pl", "Nieznane pochodzenie softwaru" },
	{ NULL, NULL }
};

static imv_lang_string_t instr_unknown_source_descr[] = {
	{ "en", "Do not allow the installation of apps from unknown sources" },
	{ "de", "Erlauben Sie nicht die Installation von Apps aus unbekannten Quellen" },
	{ "pl", "Prosz?? nie dopuszcza?? do instalacji Apps z nieznanych ??r??de??" },
	{ NULL, NULL }
};

METHOD(imv_state_t, get_connection_id, TNC_ConnectionID,
	private_imv_os_state_t *this)
{
	return this->connection_id;
}

METHOD(imv_state_t, has_long, bool,
	private_imv_os_state_t *this)
{
	return this->has_long;
}

METHOD(imv_state_t, has_excl, bool,
	private_imv_os_state_t *this)
{
	return this->has_excl;
}

METHOD(imv_state_t, set_flags, void,
	private_imv_os_state_t *this, bool has_long, bool has_excl)
{
	this->has_long = has_long;
	this->has_excl = has_excl;
}

METHOD(imv_state_t, set_max_msg_len, void,
	private_imv_os_state_t *this, u_int32_t max_msg_len)
{
	this->max_msg_len = max_msg_len;
}

METHOD(imv_state_t, get_max_msg_len, u_int32_t,
	private_imv_os_state_t *this)
{
	return this->max_msg_len;
}

METHOD(imv_state_t, set_action_flags, void,
	private_imv_os_state_t *this, u_int32_t flags)
{
	this->action_flags |= flags;
}

METHOD(imv_state_t, get_action_flags, u_int32_t,
	private_imv_os_state_t *this)
{
	return this->action_flags;
}

METHOD(imv_state_t, set_ar_id, void,
	private_imv_os_state_t *this, u_int32_t id_type, chunk_t id_value)
{
	this->ar_id_type = id_type;
	this->ar_id_value = chunk_clone(id_value);
}

METHOD(imv_state_t, get_ar_id, chunk_t,
	private_imv_os_state_t *this, u_int32_t *id_type)
{
	if (id_type)
	{
		*id_type = this->ar_id_type;
	}
	return this->ar_id_value;
}

METHOD(imv_state_t, set_session, void,
	private_imv_os_state_t *this, imv_session_t *session)
{
	this->session = session;
}

METHOD(imv_state_t, get_session, imv_session_t*,
	private_imv_os_state_t *this)
{
	return this->session;
}

METHOD(imv_state_t, get_recommendation, void,
	private_imv_os_state_t *this, TNC_IMV_Action_Recommendation *rec,
								  TNC_IMV_Evaluation_Result *eval)
{
	*rec = this->rec;
	*eval = this->eval;
}

METHOD(imv_state_t, set_recommendation, void,
	private_imv_os_state_t *this, TNC_IMV_Action_Recommendation rec,
								  TNC_IMV_Evaluation_Result eval)
{
	this->rec = rec;
	this->eval = eval;
}

METHOD(imv_state_t, update_recommendation, void,
	private_imv_os_state_t *this, TNC_IMV_Action_Recommendation rec,
								  TNC_IMV_Evaluation_Result eval)
{
	this->rec  = tncif_policy_update_recommendation(this->rec, rec);
	this->eval = tncif_policy_update_evaluation(this->eval, eval);
}

METHOD(imv_state_t, change_state, void,
	private_imv_os_state_t *this, TNC_ConnectionState new_state)
{
	this->state = new_state;
}

METHOD(imv_state_t, get_reason_string, bool,
	private_imv_os_state_t *this, enumerator_t *language_enumerator,
	chunk_t *reason_string, char **reason_language)
{
	if (!this->count_update && !this->count_blacklist & !this->os_settings)
	{
		return FALSE;
	}
	*reason_language = imv_lang_string_select_lang(language_enumerator,
											  languages, countof(languages));

	/* Instantiate a TNC Reason String object */
	DESTROY_IF(this->reason_string);
	this->reason_string = imv_reason_string_create(*reason_language);

	if (this->count_update || this->count_blacklist)
	{
		this->reason_string->add_reason(this->reason_string, reason_packages);
	}
	if (this->os_settings)
	{
		this->reason_string->add_reason(this->reason_string, reason_settings);
	}
	*reason_string = this->reason_string->get_encoding(this->reason_string);

	return TRUE;
}

METHOD(imv_state_t, get_remediation_instructions, bool,
	private_imv_os_state_t *this, enumerator_t *language_enumerator,
	chunk_t *string, char **lang_code, char **uri)
{
	if (!this->count_update && !this->count_blacklist & !this->os_settings)
	{
		return FALSE;
	}
	*lang_code = imv_lang_string_select_lang(language_enumerator,
										languages, countof(languages));

	/* Instantiate an IETF Remediation Instructions String object */
	DESTROY_IF(this->remediation_string);
	this->remediation_string = imv_remediation_string_create(
									this->type == OS_TYPE_ANDROID, *lang_code);

	/* List of blacklisted packages to be removed, if any */
	if (this->count_blacklist)
	{
		this->remediation_string->add_instruction(this->remediation_string,
							instr_remove_packages_title,
							instr_remove_packages_descr,
							instr_remove_packages_header,
							this->remove_packages);
	}

	/* List of packages in need of an update, if any */
	if (this->count_update)
	{
		this->remediation_string->add_instruction(this->remediation_string,
							instr_update_packages_title,
							instr_update_packages_descr,
							instr_update_packages_header,
							this->update_packages);
	}

	/* Add instructions concerning improper OS settings */
	if (this->os_settings & OS_SETTINGS_FWD_ENABLED)
	{
		this->remediation_string->add_instruction(this->remediation_string,
							instr_fwd_enabled_title,
							instr_fwd_enabled_descr, NULL, NULL);
	}
	if (this->os_settings & OS_SETTINGS_DEFAULT_PWD_ENABLED)
	{
		this->remediation_string->add_instruction(this->remediation_string,
							instr_default_pwd_enabled_title,
							instr_default_pwd_enabled_descr, NULL, NULL);
	}
	if (this->os_settings & OS_SETTINGS_UNKNOWN_SOURCE)
	{
		this->remediation_string->add_instruction(this->remediation_string,
							instr_unknown_source_title,
							instr_unknown_source_descr, NULL, NULL);
	}

	*string = this->remediation_string->get_encoding(this->remediation_string);
	*uri = lib->settings->get_str(lib->settings,
							"libimcv.plugins.imv-os.remediation_uri", NULL);

	return TRUE;
}

METHOD(imv_state_t, destroy, void,
	private_imv_os_state_t *this)
{
	DESTROY_IF(this->session);
	DESTROY_IF(this->reason_string);
	DESTROY_IF(this->remediation_string);
	this->update_packages->destroy_function(this->update_packages, free);
	this->remove_packages->destroy_function(this->remove_packages, free);
	free(this->info);
	free(this->name.ptr);
	free(this->version.ptr);
	free(this->ar_id_value.ptr);
	free(this->device_id.ptr);
	free(this);
}

METHOD(imv_os_state_t, set_handshake_state, void,
	private_imv_os_state_t *this, imv_os_handshake_state_t new_state)
{
	this->handshake_state = new_state;
}

METHOD(imv_os_state_t, get_handshake_state, imv_os_handshake_state_t,
	private_imv_os_state_t *this)
{
	return this->handshake_state;
}

METHOD(imv_os_state_t, set_info, void,
	private_imv_os_state_t *this, os_type_t type, chunk_t name, chunk_t version)
{
	int len = name.len + 1 + version.len + 1;

	/* OS info is a concatenation of OS name and OS version */
	free(this->info);
	this->info = malloc(len);
	snprintf(this->info, len, "%.*s %.*s", (int)name.len, name.ptr,
										   (int)version.len, version.ptr);
	this->type = type;
	this->name = chunk_clone(name);
	this->version = chunk_clone(version);
}

METHOD(imv_os_state_t, get_info, char*,
	private_imv_os_state_t *this, os_type_t *type, chunk_t *name,
	chunk_t *version)
{
	if (type)
	{
		*type = this->type;
	}
	if (name)
	{
		*name = this->name;
	}
	if (version)
	{
		*version = this->version;
	}
	return this->info;
}

METHOD(imv_os_state_t, set_count, void,
	private_imv_os_state_t *this, int count, int count_update,
	int count_blacklist, int count_ok)
{
	this->count           += count;
	this->count_update    += count_update;
	this->count_blacklist += count_blacklist;
	this->count_ok        += count_ok;
}

METHOD(imv_os_state_t, get_count, void,
	private_imv_os_state_t *this, int *count, int *count_update,
	int *count_blacklist, int *count_ok)
{
	if (count)
	{
		*count = this->count;
	}
	if (count_update)
	{
		*count_update = this->count_update;
	}
	if (count_blacklist)
	{
		*count_blacklist = this->count_blacklist;
	}
	if (count_ok)
	{
		*count_ok = this->count_ok;
	}
}

METHOD(imv_os_state_t, set_device_id, void,
	private_imv_os_state_t *this, chunk_t id)
{
	this->device_id = chunk_clone(id);
}

METHOD(imv_os_state_t, get_device_id, chunk_t,
	private_imv_os_state_t *this)
{
	return this->device_id;
}

METHOD(imv_os_state_t, set_os_settings, void,
	private_imv_os_state_t *this, u_int settings)
{
	this->os_settings |= settings;
}

METHOD(imv_os_state_t, get_os_settings, u_int,
	private_imv_os_state_t *this)
{
	return this->os_settings;
}

METHOD(imv_os_state_t, set_angel_count, void,
	private_imv_os_state_t *this, bool start)
{
	this->angel_count += start ? 1 : -1;
}

METHOD(imv_os_state_t, get_angel_count, int,
	private_imv_os_state_t *this)
{
	return this->angel_count;
}

METHOD(imv_os_state_t, add_bad_package, void,
	private_imv_os_state_t *this, char *package,
	os_package_state_t package_state)
{
	package = strdup(package);

	if (package_state == OS_PACKAGE_STATE_BLACKLIST)
	{
		this->remove_packages->insert_last(this->remove_packages, package);
	}
	else
	{
		this->update_packages->insert_last(this->update_packages, package);
	}
}

/**
 * Described in header.
 */
imv_state_t *imv_os_state_create(TNC_ConnectionID connection_id)
{
	private_imv_os_state_t *this;

	INIT(this,
		.public = {
			.interface = {
				.get_connection_id = _get_connection_id,
				.has_long = _has_long,
				.has_excl = _has_excl,
				.set_flags = _set_flags,
				.set_max_msg_len = _set_max_msg_len,
				.get_max_msg_len = _get_max_msg_len,
				.set_action_flags = _set_action_flags,
				.get_action_flags = _get_action_flags,
				.set_ar_id = _set_ar_id,
				.get_ar_id = _get_ar_id,
				.set_session = _set_session,
				.get_session = _get_session,
				.change_state = _change_state,
				.get_recommendation = _get_recommendation,
				.set_recommendation = _set_recommendation,
				.update_recommendation = _update_recommendation,
				.get_reason_string = _get_reason_string,
				.get_remediation_instructions = _get_remediation_instructions,
				.destroy = _destroy,
			},
			.set_handshake_state = _set_handshake_state,
			.get_handshake_state = _get_handshake_state,
			.set_info = _set_info,
			.get_info = _get_info,
			.set_count = _set_count,
			.get_count = _get_count,
			.set_device_id = _set_device_id,
			.get_device_id = _get_device_id,
			.set_os_settings = _set_os_settings,
			.get_os_settings = _get_os_settings,
			.set_angel_count = _set_angel_count,
			.get_angel_count = _get_angel_count,
			.add_bad_package = _add_bad_package,
		},
		.state = TNC_CONNECTION_STATE_CREATE,
		.rec = TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION,
		.eval = TNC_IMV_EVALUATION_RESULT_DONT_KNOW,
		.connection_id = connection_id,
		.update_packages = linked_list_create(),
		.remove_packages = linked_list_create(),
	);

	return &this->public.interface;
}


