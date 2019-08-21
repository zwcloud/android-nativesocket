//
// Created by yutianzuo on 2019-08-02.
//

#ifndef SIMPLESOCKET_DNS_H
#define SIMPLESOCKET_DNS_H

#include "../socket/simpleudpclient.h"
#include "../toolbox/timeutils.h"
#include "../toolbox/miscs.h"
#include "../toolbox/string_x.h"
#include "errorhunter.h"

#pragma pack(1)
struct DnsHeader
{
    std::int16_t trans_id;
    std::int8_t flags[2];
    std::int16_t questions;
    std::int16_t answer_rrs;
    std::int16_t authority_rrs;
    std::int16_t additional_rrs;
};
#pragma pack()


class DNSQuery final : public SimpleUdpClient
{
public:
    enum
    {
        DNS_UDP_PORT = 53,
    };
#if(__cplusplus >= 201103L)

    DNSQuery(const DNSQuery &) = delete;

    DNSQuery(DNSQuery &&s) noexcept : SimpleUdpClient(std::move(s))
    {
    }

    void operator=(const DNSQuery &) = delete;

    void operator=(DNSQuery &&) = delete;

#endif

    DNSQuery() = default;

    ~DNSQuery() override = default;

    ///query dns server, only A records; block func
    bool get_ip_by_host(const std::string &str_dnsip, const std::string &str_host_query, std::vector<std::string> &ips)
    {
        bool ret = false;
        FUNCTION_BEGIN ;
            if (!SimpleUdpClient::init_unconnected(str_dnsip, DNS_UDP_PORT))
            {
                m_last_errmsg = NORMAL_ERROR_MSG("init_unconnected failed");
                FUNCTION_LEAVE;
            }
            SimpleUdpClient::set_check_data(false);
            SimpleUdpClient::set_non_blocking(true);
            if (!SimpleUdpClient::operator<<(dns_query(str_host_query)))
            {
                m_last_errmsg = NORMAL_ERROR_MSG("send msg failed");
                FUNCTION_LEAVE;
            }

            ////
            int counter = 0;
            std::string respones;
            while (counter++ < 20)
            {
                if (SimpleUdpClient::operator>>(respones))
                {
                    break;
                }
                TimeUtils::sleep_for_millis(100);
            }

            if (!respones.empty())
            {
                ret = analyze(respones, ips);
            }
            else
            {
                m_last_errmsg = NORMAL_ERROR_MSG("respones is empty");
            }
        FUNCTION_END;
        return ret;
    }

private:
    int trans_id = 0;

    ///construct simple dns query request
    std::string dns_query(std::string const &str_host)
    {
        std::string request;
        const int buff_len = 256;
        char buff[buff_len] = {0};
        int index = 0;

        ::srand(::time(nullptr));
        trans_id = rand();

        stringxa strx_host(str_host);
        strx_host.trim();
        std::vector<stringxa> hosts;
        strx_host.split_string(".", hosts);

        ///header
        DnsHeader header = {0};
        header.trans_id = htons((std::int16_t)trans_id);

        header.flags[0] = 1;
        header.flags[1] = 0;//flags  0 0000 0 1 0000 0000

        header.questions = htons(1);

        header.answer_rrs = htons(0);

        header.authority_rrs = htons(0);

        header.additional_rrs = htons(0);

        memcpy(buff, &header, sizeof(header));
        index += sizeof(header);
        ///header end

        ///queries
        for (const auto &host : hosts)
        {
            buff[index++] = host.size();
            for (auto c : host)
            {
                buff[index++] = c;
            }
        }
        index++; //end with a ZERO

        buff[index++] = 0;
        buff[index++] = 1; //type A --> 0x0001

        buff[index++] = 0;
        buff[index++] = 1; //class IN --> 0x0001

        ///queries end

        request.assign(buff, index > buff_len ? buff_len : index);
        return request;
    }
//////////////////////////////////////////////////////////////////////////////////////////////

    ///analyze respones
    bool analyze(const std::string &str_respones, std::vector<std::string> &str_ips)
    {
        bool ret = false;
        FUNCTION_BEGIN ;
            int index = 0;
            int answers = 0;
            ///header
            if (!analyze_header(index, str_respones, answers))
            {
                FUNCTION_LEAVE;
            }
            ///header end

            ///queries
            std::string str_host;
            if (!analyze_queries(index, str_respones, str_host))
            {
                m_last_errmsg = NORMAL_ERROR_MSG("analyze_queries failed");
                FUNCTION_LEAVE;
            }
            ///queries end

            ///answers
            if (!analyze_answers(index, str_respones, answers, str_ips))
            {
                m_last_errmsg = NORMAL_ERROR_MSG("analyze_answers failed");
                FUNCTION_LEAVE;
            }
            ///answers end

            ret = true;
        FUNCTION_END;
        return ret;
    }

    bool analyze_answers(int &index, const std::string &str_respones, int answers, std::vector<std::string> &ips)
    {
        bool ret = false;
        int index_inner = index;
        FUNCTION_BEGIN ;
            if (!check_index(index_inner, str_respones))
            {
                FUNCTION_LEAVE;
            }

            //2bytes-->Name; 2bytes-->type; 2bytes-->class; 4bytes-->time; 2bytes-->lenth; 4bytes-->ipv4;
            if (index_inner + answers * 16 > str_respones.size()) //check A record * count bytes
            {
                m_last_errmsg = NORMAL_ERROR_MSG("answer's size error");
                FUNCTION_LEAVE;
            }

            for (int i = 0; i < answers; ++i)
            {
                index_inner += 2; //jump name

                std::uint16_t type = ntohs(* ((std::int16_t*) &str_respones[index_inner]));//type
                index_inner += 2;
                std::uint16_t query_class = ntohs(*((std::int16_t*) &str_respones[index_inner]));//class
                index_inner += 2;

                index_inner += 4; //jump live time

                std::uint16_t len = ntohs(* ((std::int16_t*) &str_respones[index_inner]));//length
                index_inner += 2;
                if (type != 1 || query_class != 1 || len != 4) // like cname
                {
                    index_inner += len;
                    continue;
                }
                int ip = *((int *) (&str_respones[index_inner]));
                std::string str_ip = NetHelper::int32_to_string_addr(ip);
                ips.emplace_back(std::move(str_ip));
                index_inner += 4;
            }

            index = index_inner;
            ret = true;
        FUNCTION_END;
        return ret;
    }

    bool analyze_queries(int &index, const std::string &str_respones, std::string &str_host)
    {
        bool ret = false;
        int index_inner = index;
        str_host.clear();
        FUNCTION_BEGIN ;
            if (!check_index(index_inner, str_respones))
            {
                FUNCTION_LEAVE;
            }

            int jump = 0;
            while (str_respones[index_inner] != 0)
            {
                if (!check_index(index_inner, str_respones))
                {
                    break;
                }
                if (jump == 0)
                {
                    jump = (unsigned char) str_respones[index_inner++];
                }
                else
                {
                    if (index_inner + jump <= str_respones.size())
                    {
                        str_host.append(&str_respones[index_inner], jump);
                        str_host.append(".");
                    }
                    index_inner += jump;
                    jump = 0;
                }
            }
            if (str_host.size())
            {
                str_host.erase(str_host.size() - 1);
            }
            ++index_inner; //jump '\0'

            if (!check_index(index_inner + 4, str_respones))
            {
                FUNCTION_LEAVE;
            }

            std::uint16_t type = ntohs(* ((std::int16_t*) &str_respones[index_inner]));//type shoud be an A(ipv4)
            index_inner += 2;
            std::uint16_t query_class = ntohs(*((std::int16_t*) &str_respones[index_inner]));//class should be IN
            index_inner += 2;

            if (type != 1 || query_class != 1)
            {
                FUNCTION_LEAVE;
            }
            index = index_inner;
            ret = true;
        FUNCTION_END;
        return ret;
    }

    bool analyze_header(int &index, const std::string &str_respones, int &answers)
    {
        bool ret = false;
        int index_inner = index;
        FUNCTION_BEGIN ;
            if (str_respones.size() < sizeof(DnsHeader))
            {
                FUNCTION_LEAVE;
            }
            DnsHeader* header = (DnsHeader*)(&str_respones[index_inner]);
            if ((std::int16_t)(ntohs(header->trans_id)) != (std::int16_t)trans_id)
            {
                m_last_errmsg = NORMAL_ERROR_MSG("transid not match");
                FUNCTION_LEAVE;
            } //check trans_id

            if (!analyze_flags((const char*)header->flags))
            {
                FUNCTION_LEAVE;
            } //check flags

            int questions = ntohs(header->questions);
            if (questions != 1)
            {
                m_last_errmsg = NORMAL_ERROR_MSG("question count is not 1");
                FUNCTION_LEAVE;
            }

            answers = ntohs(header->answer_rrs);

            index_inner += sizeof(DnsHeader); //authority rrs,additioanl rrs

            ret = true;
        FUNCTION_END;
        if (ret)
        {
            index = index_inner;
        }

        return ret;
    }

    bool analyze_flags(const char *buff)
    {
        bool ret = false;
        FUNCTION_BEGIN ;
            //x0000000
            int respones_type = (buff[0] & 0x80) >> 7; //respones type: 1,message is a response 0,message is a request
            if (respones_type != 1)
            {
                m_last_errmsg = NORMAL_ERROR_MSG("flag is not a respones");
                FUNCTION_LEAVE;
            }
            //0xxxx000
            int opcode =
                    (buff[0] & 0x78) >> 3; //opcode: 0,is a standard query 1,is a reverse query 2, server status request
            //00000x00
            int authorit_answer =
                    (buff[0] & 0x04) >> 2; //AA, Authoritative answer. 0,server is not an authority for domain
            //000000x0
            int truncated = (buff[0] & 0x02) >> 1; //TC, 0, message is not truncated. 1, truncated.
            //0000000x
            int recursion_desired = buff[0] & 0x01; //RD, 1,do query recursively 0, not recursively

            //x0000000
            int recursion_available = (buff[1] & 0x80) >> 7; //RA, 1,server can do recursive queries, 2, not
            //0x000000
            //Z(reserved 0)

            //00x00000
            int answer_authenticated =
                    (buff[1] & 0x20) >> 5; //Answer authenticated: 0, Answer/authority portion was not
            //authenticated by the server

            //000x0000
            int non_authenticated_data = (buff[1] & 0x10) >> 4; //0, Unacceptable

            //0000xxxx
            int reply_code = (buff[1] & 0x0f); //reply code: 0,no error 2,server failure 3,name error
            if (reply_code != 0)
            {
                std::string error = "reply code error:";
                error += std::to_string(reply_code);
                m_last_errmsg = NORMAL_ERROR_MSG(error);
                FUNCTION_LEAVE;
            }
            ret = true;
        FUNCTION_END;
        return ret;
    }


    bool check_index(int index, const std::string &respones)
    {
        if (index >= respones.size())
        {
            return false;
        }
        return true;
    }
};

#endif //SIMPLESOCKET_DNS_H